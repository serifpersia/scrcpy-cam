#include "session.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "adb.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

static const char* SERVER_VERSION = "5.0.1";
static const char* DEVICE_SERVER_PATH = "/data/local/tmp/scrcpy-server.jar";

#ifdef _WIN32
  #include <process.h>
  static unsigned current_pid() { return (unsigned)_getpid(); }
#else
  #include <unistd.h>
  static unsigned current_pid() { return (unsigned)getpid(); }
#endif

namespace {

uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint64_t read_be64(const uint8_t* p) {
    return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

void put_be32(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

const uint64_t PACKET_FLAG_CONFIG = UINT64_C(1) << 62;
const uint64_t PACKET_FLAG_KEY_FRAME = UINT64_C(1) << 61;
const uint64_t PACKET_PTS_MASK = PACKET_FLAG_KEY_FRAME - 1;

void pump_video(socket_t video,
                const std::function<void(const uint8_t*, int, int, int)>& on_frame,
                const std::function<void(int, int, int)>& on_status) {
    net_set_timeout(video, 0);

    uint8_t codec[4];
    if (!net_recv_full(video, codec, 4)) return;

    VideoDecoder dec;
    std::vector<uint8_t> payload;
    int fps_count = 0;
    int cur_w = 0, cur_h = 0;
    auto fps_window = std::chrono::steady_clock::now();

    for (;;) {
        uint8_t hdr[12];
        if (!net_recv_full(video, hdr, 12)) break;

        if (hdr[0] & 0x80) {
            cur_w = (int)read_be32(hdr + 4);
            cur_h = (int)read_be32(hdr + 8);
            if (on_status) on_status(cur_w, cur_h, 0);
            continue;
        }

        uint64_t pts_flags = read_be64(hdr);
        uint32_t len = read_be32(hdr + 8);
        if (!len || len > (32u << 20)) break;

        payload.resize(len);
        if (!net_recv_full(video, payload.data(), len)) break;

        int64_t pts = (int64_t)(pts_flags & PACKET_PTS_MASK);
        bool config = (pts_flags & PACKET_FLAG_CONFIG) != 0;

        int produced = config ? dec.feed_config(payload.data(), len, pts)
                              : dec.decode(payload.data(), len, pts);
        if (!produced) continue;

        cur_w = dec.out_w;
        cur_h = dec.out_h;
        if (on_frame) on_frame(dec.rgb.data(), dec.out_w, dec.out_h, (dec.out_w * 3 + 3) & ~3);

        if (++fps_count >= 15) {
            auto now = std::chrono::steady_clock::now();
            double secs = std::chrono::duration<double>(now - fps_window).count();
            int fps = secs > 0 ? (int)(fps_count / secs) : 0;
            fps_count = 0;
            fps_window = now;
            if (on_status) on_status(cur_w, cur_h, fps);
        }
    }
}

void run_video(Session* s) {
    pump_video(
        s->video,
        [s](const uint8_t* bgr, int w, int h, int stride) {
#ifdef _WIN32
            if (s->vcam.ok()) {
                s->vcam.push_rgb24(bgr, w, h, s->cfg.camera_fps ? s->cfg.camera_fps : 30);
            }
#else
            if (s->v4l2.ok()) {
                s->v4l2.push(bgr, w, h, stride, s->cfg.camera_fps ? s->cfg.camera_fps : 30);
            }
#endif
            if (s->preview_enabled.load()) {
                std::lock_guard<std::mutex> lk(s->preview_mutex);
                s->preview_rgb.resize((size_t)w * (size_t)h * 3);
                for (int y = 0; y < h; ++y)
                    std::memcpy(s->preview_rgb.data() + (size_t)y * w * 3,
                                bgr + (size_t)y * stride, (size_t)w * 3);
                s->preview_w = w;
                s->preview_h = h;
                ++s->preview_seq;
            }
            s->frames_flowing = true;
        },
        [s](int w, int h, int fps) {
            if (w) s->stream_w = w;
            if (h) s->stream_h = h;
            s->measured_fps = fps;
        });
    s->frames_flowing = false;
    s->measured_fps = 0;
}

static bool recv_audio_frame(socket_t s, uint64_t* pts, std::vector<uint8_t>* data) {
    uint8_t hdr[12];
    if (!net_recv_full(s, hdr, sizeof(hdr))) return false;
    uint64_t meta = ((uint64_t)hdr[0] << 56) | ((uint64_t)hdr[1] << 48) |
                    ((uint64_t)hdr[2] << 40) | ((uint64_t)hdr[3] << 32) |
                    ((uint64_t)hdr[4] << 24) | ((uint64_t)hdr[5] << 16) |
                    ((uint64_t)hdr[6] << 8) | (uint64_t)hdr[7];
    uint32_t size = ((uint32_t)hdr[8] << 24) | ((uint32_t)hdr[9] << 16) |
                    ((uint32_t)hdr[10] << 8) | (uint32_t)hdr[11];
    *pts = meta & ((1ULL << 62) - 1);
    if (meta & (1ULL << 63)) {
        data->assign(size, 0);
        return net_recv_full(s, data->data(), size);
    }
    data->resize(size);
    return net_recv_full(s, data->data(), size);
}

static void pump_audio_s16(const uint8_t* data, size_t size, SDL_AudioStream* out,
                           std::atomic<bool>* stopping, std::atomic<bool>* mute) {
    size_t off = 0;
    while (off < size && !stopping->load()) {
        size_t chunk = std::min<size_t>(size - off, 4096);
        if (!mute->load() && out) SDL_PutAudioStreamData(out, data + off, (int)chunk);
        off += chunk;
    }
}

static void run_audio(Session* s) {
    uint8_t codec_raw[4];
    if (!net_recv_full(s->audio, codec_raw, 4)) return;
    uint32_t codec = ((uint32_t)codec_raw[0] << 24) | ((uint32_t)codec_raw[1] << 16) |
                     ((uint32_t)codec_raw[2] << 8) | (uint32_t)codec_raw[3];
    net_set_timeout(s->audio, 0);

    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
    if (codec == 0x00726177) {
        SDL_AudioDeviceID dev = s->cfg.audio_device ? s->cfg.audio_device : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
        s->audio_spec = spec;
        s->audio_out = SDL_OpenAudioDeviceStream(dev, &spec, nullptr, nullptr);
        if (s->audio_out && !s->cfg.mute_audio) SDL_ResumeAudioStreamDevice(s->audio_out);
        std::vector<uint8_t> pkt;
        uint64_t pts = 0;
        while (!s->stopping.load()) {
            if (!recv_audio_frame(s->audio, &pts, &pkt)) break;
            if (pkt.empty()) continue;
            if (!s->cfg.mute_audio && s->audio_out)
                SDL_PutAudioStreamData(s->audio_out, pkt.data(), (int)pkt.size());
        }
        if (s->audio_out) { SDL_DestroyAudioStream(s->audio_out); s->audio_out = nullptr; }
        return;
    }

    const AVCodec* avc = avcodec_find_decoder(codec == 0x6F707573 ? AV_CODEC_ID_OPUS : AV_CODEC_ID_AAC);
    if (!avc) return;
    AVCodecContext* ctx = avcodec_alloc_context3(avc);
    if (!ctx) return;
    ctx->sample_rate = 48000;
    AVChannelLayout ch = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&ctx->ch_layout, &ch);
    if (avcodec_open2(ctx, avc, nullptr) < 0) { avcodec_free_context(&ctx); return; }

    SwrContext* swr = nullptr;
    swr_alloc_set_opts2(&swr, &ch, AV_SAMPLE_FMT_S16, 48000, &ctx->ch_layout, ctx->sample_fmt,
                        ctx->sample_rate > 0 ? ctx->sample_rate : 48000, 0, nullptr);
    if (!swr || swr_init(swr) < 0) { if (swr) swr_free(&swr); avcodec_free_context(&ctx); return; }

    SDL_AudioDeviceID dev = s->cfg.audio_device ? s->cfg.audio_device : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    s->audio_spec = spec;
    s->audio_out = SDL_OpenAudioDeviceStream(dev, &spec, nullptr, nullptr);
    if (s->audio_out && !s->cfg.mute_audio) SDL_ResumeAudioStreamDevice(s->audio_out);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<uint8_t> data;
    std::vector<uint8_t> pcm;
    uint64_t pts = 0;
    while (!s->stopping.load()) {
        if (!recv_audio_frame(s->audio, &pts, &data)) break;
        av_packet_unref(pkt);
        pkt->data = data.data();
        pkt->size = (int)data.size();
        if (avcodec_send_packet(ctx, pkt) < 0) continue;
        while (avcodec_receive_frame(ctx, frame) >= 0) {
            int out_samples = (int)av_rescale_rnd(swr_get_delay(swr, ctx->sample_rate) + frame->nb_samples,
                                                  48000, ctx->sample_rate, AV_ROUND_UP);
            pcm.resize((size_t)out_samples * 4);
            uint8_t* outp[] = {pcm.data()};
            int n = swr_convert(swr, outp, out_samples, (const uint8_t**)frame->extended_data, frame->nb_samples);
            if (n > 0 && !s->cfg.mute_audio && s->audio_out)
                SDL_PutAudioStreamData(s->audio_out, pcm.data(), n * 4);
        }
    }
    if (s->audio_out) { SDL_DestroyAudioStream(s->audio_out); s->audio_out = nullptr; }
    av_frame_free(&frame); av_packet_free(&pkt); swr_free(&swr); avcodec_free_context(&ctx);
}
bool connect_with_retry(uint16_t port, const std::string& socket_name, unsigned attempts, socket_t* out, bool read_dummy) {
    for (unsigned i = 0; i < attempts; ++i) {
        socket_t sock = net_connect_tcp("127.0.0.1", port);
        if (sock != INVALID_SOCKET_VAL) {
            if (!read_dummy) {
                *out = sock;
                return true;
            }
            net_set_timeout(sock, 15000);
            char dummy;
            if (net_recv_full(sock, &dummy, 1)) {
                *out = sock;
                return true;
            }
            net_close(sock);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

}

bool Session::init_output() {
#ifdef _WIN32
    if (vcam.ok()) return true;
    if (vcam.init()) {
        vcam.set_mirror(flip_x ? 1 : 0);
        vcam.set_flip(flip_y ? 1 : 0);
        vcam.set_rotate(rotate ? 1 : 0);
        return true;
    }
    fprintf(stderr,
            "[vcam] SDK not available: the virtual camera will not be created.\n"
            "        Install the e2eSoft VCam driver and make sure its DLL is registered.\n");
    return false;
#else
    if (v4l2.ok()) return true;
    std::string dev = output_device.empty() ? "/dev/video0" : output_device;
    if (v4l2.init(dev)) {
        printf("[v4l2] writing to %s\n", dev.c_str());
        return true;
    }
    fprintf(stderr, "[v4l2] %s: %s\n", dev.c_str(), v4l2.last_error());
    fprintf(stderr, "        run: sudo modprobe v4l2loopback\n");
    return false;
#endif
}

void Session::shutdown_output() {
#ifdef _WIN32
    vcam.shutdown();
#else
    v4l2.shutdown();
#endif
}

bool Session::output_ok() const {
#ifdef _WIN32
    return vcam.ok();
#else
    return v4l2.ok();
#endif
}

bool Session::start(std::string* err) {
    if (!net_init()) {
        *err = "could not initialize sockets";
        return false;
    }

    adb_set_serial(cfg.serial);
    if (cfg.serial.find(':') != std::string::npos && adb_connect(cfg.serial) != 0) {
        *err = "could not connect to " + cfg.serial + " (check wireless debugging)";
        return false;
    }
    init_output();

    char scid[16];
    snprintf(scid, sizeof(scid), "%08x", current_pid());

    std::string socket_name = std::string("scrcpy_") + scid;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)cfg.port);

    if (!adb_push(cfg.server_apk, DEVICE_SERVER_PATH)) {
        *err = "could not push the scrcpy server";
        return false;
    }
    if (!adb_forward(cfg.port, socket_name)) {
        *err = "could not open the adb forward";
        return false;
    }

    std::string args = std::string("shell CLASSPATH=") + DEVICE_SERVER_PATH +
                       " app_process / com.genymobile.scrcpy.Server " + SERVER_VERSION +
                       " scid=" + scid +
                       " log_level=debug"
                       " video_source=camera"
                       " tunnel_forward=true";
    args += cfg.mic ? " audio=true audio_source=mic-camcorder audio_codec=raw" : " audio=false";
char buf[32];
    if (!cfg.camera_size.empty()) {
        snprintf(buf, sizeof(buf), "camera_size=%s", cfg.camera_size.c_str());
        args += std::string(" ") + buf;
    }
    if (!cfg.camera_id.empty()) {
        snprintf(buf, sizeof(buf), "camera_id=%s", cfg.camera_id.c_str());
        args += std::string(" ") + buf;
    }
    if (cfg.camera_fps > 0) {
        snprintf(buf, sizeof(buf), "camera_fps=%d", cfg.camera_fps);
        args += std::string(" ") + buf;
    }
    if (cfg.video_bit_rate > 0) {
        snprintf(buf, sizeof(buf), "video_bit_rate=%d", cfg.video_bit_rate);
        args += std::string(" ") + buf;
    }
    if (cfg.torch) args += " camera_torch=true";
    if (cfg.zoom > 0) args += " camera_zoom=" + std::to_string(cfg.zoom);

    if (!adb_spawn_detached(args, "cam_client_server.log")) {
        *err = "could not start the scrcpy server";
        adb_forward_remove(cfg.port);
        return false;
    }

    if (!connect_with_retry(cfg.port, socket_name, 100, &video, true)) {
        *err = "could not connect to the scrcpy server (is the camera already in use?)";
        stop();
        return false;
    }
    if (cfg.mic && !connect_with_retry(cfg.port, socket_name, 100, &audio, false)) {
        *err = "could not connect audio stream";
        stop();
        return false;
    }
    if (!connect_with_retry(cfg.port, socket_name, 100, &control, false)) {
        *err = "could not connect control channel";
        stop();
        return false;
    }

    char device_name[65];
    if (!net_recv_full(video, device_name, 64)) {
        *err = "could not read the device name";
        stop();
        return false;
    }
    device_name[64] = '\0';


    stopping = false;
    connected = true;
    video_thread = std::thread(run_video, this);
    if (cfg.mic && audio != INVALID_SOCKET_VAL)
        audio_thread = std::thread(run_audio, this);
    return true;
}

void Session::stop() {
    connected = false;
    stopping = true;

    if (control != INVALID_SOCKET_VAL) net_shutdown(control);
    if (audio != INVALID_SOCKET_VAL) net_shutdown(audio);
    if (video != INVALID_SOCKET_VAL) net_shutdown(video);

    if (video_thread.joinable()) video_thread.join();
    if (audio_thread.joinable()) audio_thread.join();

    if (control != INVALID_SOCKET_VAL) {
        net_close(control);
        control = INVALID_SOCKET_VAL;
    }
    if (audio != INVALID_SOCKET_VAL) {
        net_close(audio);
        audio = INVALID_SOCKET_VAL;
    }
    if (video != INVALID_SOCKET_VAL) {
        net_close(video);
        video = INVALID_SOCKET_VAL;
    }

    adb_forward_remove(cfg.port);
    frames_flowing = false;
    measured_fps = 0;
    stream_w = 0;
    stream_h = 0;
}

bool Session::send_param(int param, int32_t value) {
    if (control == INVALID_SOCKET_VAL) return false;
    uint8_t buf[6];
    buf[0] = MSG_CAMERA_SET_PARAM;
    buf[1] = (uint8_t)param;
    put_be32(buf + 2, value);
    return net_send(control, buf, sizeof(buf));
}

bool Session::set_torch(bool on) {
    if (control == INVALID_SOCKET_VAL) return false;
    uint8_t buf[2] = {MSG_CAMERA_SET_TORCH, (uint8_t)(on ? 1 : 0)};
    return net_send(control, buf, sizeof(buf));
}

bool Session::zoom_in() {
    if (control == INVALID_SOCKET_VAL) return false;
    uint8_t b = MSG_CAMERA_ZOOM_IN;
    return net_send(control, &b, 1);
}

bool Session::zoom_out() {
    if (control == INVALID_SOCKET_VAL) return false;
    uint8_t b = MSG_CAMERA_ZOOM_OUT;
    return net_send(control, &b, 1);
}

void Session::set_flip_x(bool on) {
    flip_x = on;
#ifdef _WIN32
    vcam.set_mirror(on ? 1 : 0);
#endif
}

void Session::set_flip_y(bool on) {
    flip_y = on;
#ifdef _WIN32
    vcam.set_flip(on ? 1 : 0);
#endif
}

void Session::set_rotate(bool on) {
    rotate = on;
#ifdef _WIN32
    vcam.set_rotate(on ? 1 : 0);
#endif
}

void Session::set_audio(bool mic, bool mute, uint32_t device) {
    cfg.mic = mic;
    cfg.mute_audio = mute;
    cfg.audio_device = device;
    if (audio_out) {
        if (mute) SDL_PauseAudioStreamDevice(audio_out);
        else SDL_ResumeAudioStreamDevice(audio_out);
    }
}

void Session::set_audio_device(uint32_t device) {
    cfg.audio_device = device;
}

bool query_cameras(const std::string& serial, const std::string& server_apk,
                   std::vector<CameraInfo>* out, std::string* err) {
    adb_set_serial(serial);
    if (serial.find(':') != std::string::npos && adb_connect(serial) != 0) {
        *err = "could not connect to " + serial + " (check wireless debugging)";
        return false;
    }
    if (!adb_push(server_apk, DEVICE_SERVER_PATH)) {
        *err = "could not push the scrcpy server";
        return false;
    }

    std::string out_text;
    std::string args =
        "shell CLASSPATH=" + std::string(DEVICE_SERVER_PATH) +
        " app_process / com.genymobile.scrcpy.Server " + SERVER_VERSION +
        " scid=cafe01 log_level=info list_cameras=true list_camera_sizes=true";
    if (!adb_capture(args, &out_text)) {
        *err = "could not run the server";
        return false;
    }
    if (!parse_camera_list(out_text, out)) {
        *err = "could not parse the camera list";
        return false;
    }
    return true;
}


