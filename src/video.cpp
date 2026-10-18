#include "video.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <cstring>

void VideoDecoder::reset() {
    if (sws)  { sws_freeContext(sws); sws = nullptr; }
    if (frame) { av_frame_free(&frame); frame = nullptr; }
    if (pkt)   { av_packet_free(&pkt); pkt = nullptr; }
    if (ctx)   { avcodec_free_context(&ctx); ctx = nullptr; }
    extradata.clear();
    out_w = out_h = 0;
    inited = false;
}

bool VideoDecoder::init_internal() {
    av_log_set_level(AV_LOG_QUIET);

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) return false;

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;

    if (!extradata.empty()) {
        ctx->extradata = (uint8_t*)av_malloc(extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE);
        std::memcpy(ctx->extradata, extradata.data(), extradata.size());
        ctx->extradata_size = (int)extradata.size();
    }

    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(ctx, codec, nullptr) < 0) return false;

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) return false;

    inited = true;
    return true;
}

int VideoDecoder::feed_config(const uint8_t* data, size_t size, int64_t pts) {
    if (!inited) {
        extradata.insert(extradata.end(), data, data + size);
        if (!init_internal()) return 0;
    } else {
        avcodec_flush_buffers(ctx);
    }
    return decode(data, size, pts);
}

int VideoDecoder::decode(const uint8_t* data, size_t size, int64_t pts) {
    if (!inited) return 0;

    av_packet_unref(pkt);
    pkt->data = const_cast<uint8_t*>(data);
    pkt->size = (int)size;
    pkt->pts = pts;

    int produced = 0;
    if (avcodec_send_packet(ctx, pkt) >= 0) {
        while (avcodec_receive_frame(ctx, frame) >= 0) {
            int w = frame->width;
            int h = frame->height;

            if (!sws || out_w != w || out_h != h) {
                if (sws) sws_freeContext(sws);
                sws = sws_getContext(w, h, (AVPixelFormat)frame->format,
                                     w, h, AV_PIX_FMT_BGR24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!sws) return 0;
                out_w = w;
                out_h = h;
                int stride = (out_w * 3 + 3) & ~3;
                rgb.assign((size_t)stride * out_h, 0);
            }

            uint8_t* dst[1] = { rgb.data() };
            int dst_stride[1] = { (out_w * 3 + 3) & ~3 };
            sws_scale(sws, frame->data, frame->linesize, 0, out_h, dst, dst_stride);
            produced = 1;
        }
    }
    return produced;
}
