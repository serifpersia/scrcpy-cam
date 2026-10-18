#pragma once
#include <cstdint>
#include <string>

struct V4L2Sink {
    int fd = -1;
    int width = 0, height = 0;
    int bytesperline = 0, sizeimage = 0;
    bool configured = false;

    bool ok() const { return fd >= 0; }

    bool init(const std::string& device);
    void shutdown();

    void push(const uint8_t* bgr, int w, int h, int stride, int fps);
    const char* last_error() const { return err_; }

private:
    const char* err_ = "";
    struct SwsContext* sws_ = nullptr;
    uint8_t* yuv_ = nullptr;
    size_t yuv_size_ = 0;
};
