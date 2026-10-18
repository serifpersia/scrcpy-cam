#pragma once
#include <atomic>
#include <string>
#include <thread>

#include "cameras.h"
#include "net.h"
#include "video.h"

#ifdef _WIN32
  #include "vcam.h"
#else
  #include "v4l2_sink.h"
#endif

struct SessionConfig {
    std::string serial;
    std::string server_apk;

    std::string camera_id;
    std::string camera_size;
    int camera_fps = 0;
    int max_size = 1280;
    int video_bit_rate = 0;
    bool high_speed = false;
    bool torch = false;
    float zoom = 0.f;
    std::string output_device;
    uint16_t port = 27183;
};

enum {
    MSG_CAMERA_SET_TORCH = 18,
    MSG_CAMERA_ZOOM_IN = 19,
    MSG_CAMERA_ZOOM_OUT = 20,
    MSG_CAMERA_SET_PARAM = 23,
};

enum {
    PARAM_AF_MODE = 0,
    PARAM_FOCUS_DISTANCE = 1,
    PARAM_AE_MODE = 2,
    PARAM_EXPOSURE_TIME = 3,
    PARAM_SENSITIVITY = 4,
    PARAM_AF_TRIGGER = 5,
};

enum { AF_OFF = 0, AF_AUTO = 1, AE_OFF = 0, AE_AUTO = 1 };

struct Session {
    SessionConfig cfg;

    socket_t video = INVALID_SOCKET_VAL;
    socket_t control = INVALID_SOCKET_VAL;

    std::thread video_thread;
    std::atomic<bool> stopping{false};
    std::atomic<bool> connected{false};

    std::atomic<int> stream_w{0}, stream_h{0};
    std::atomic<int> measured_fps{0};
    std::atomic<bool> frames_flowing{false};

#ifdef _WIN32
    VCamRenderer vcam;
#else
    V4L2Sink v4l2;
#endif
    std::string output_device;

    bool init_output();
    void shutdown_output();
    bool output_ok() const;

    bool start(std::string* err);
    void stop();

    bool send_param(int param, int32_t value);
    bool set_torch(bool on);
    bool zoom_in();
    bool zoom_out();
};

bool query_cameras(const std::string& serial, const std::string& server_apk,
                   std::vector<CameraInfo>* out, std::string* err);

#include <functional>

void pump_video(socket_t video, const std::function<void(const uint8_t* bgr, int w, int h,
                                                         int stride)>& on_frame,
                const std::function<void(int w, int h, int fps)>& on_status);
