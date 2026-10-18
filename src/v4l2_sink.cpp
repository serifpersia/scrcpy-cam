#include "v4l2_sink.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

bool V4L2Sink::init(const std::string& device) {
    shutdown();

    fd = ::open(device.c_str(), O_RDWR);
    if (fd < 0) {
        err_ = "cannot open the v4l2 device (did you 'modprobe v4l2loopback'?)";
        return false;
    }
    return true;
}

void V4L2Sink::shutdown() {
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (yuv_) {
        av_free(yuv_);
        yuv_ = nullptr;
        yuv_size_ = 0;
    }
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    configured = false;
}

void V4L2Sink::push(const uint8_t* bgr, int w, int h, int stride, int fps) {
    (void)fps;
    if (fd < 0 || !bgr || w <= 0 || h <= 0) return;

    if (!configured || w != width || h != height) {
        struct v4l2_format fmt;
        std::memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        fmt.fmt.pix.width = (uint32_t)w;
        fmt.fmt.pix.height = (uint32_t)h;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
            err_ = "VIDIOC_S_FMT failed";
            fprintf(stderr, "[v4l2] %s (device does not accept YUV420 %dx%d)\n", err_, w, h);
            return;
        }

        width = fmt.fmt.pix.width;
        height = fmt.fmt.pix.height;
        bytesperline = fmt.fmt.pix.bytesperline;
        sizeimage = fmt.fmt.pix.sizeimage;
        configured = true;

        if (sws_) sws_freeContext(sws_);
        sws_ = sws_getContext(w, h, AV_PIX_FMT_BGR24, width, height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_) {
            err_ = "sws_getContext failed";
            fprintf(stderr, "[v4l2] %s\n", err_);
            return;
        }

        yuv_size_ = (size_t)sizeimage;
        yuv_ = (uint8_t*)av_malloc(yuv_size_ + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!yuv_) {
            err_ = "out of memory";
            return;
        }
    }

    if (!sws_ || !yuv_) return;

    const uint8_t* src[4] = {bgr, nullptr, nullptr, nullptr};
    int src_stride[4] = {stride, 0, 0, 0};
    uint8_t* dst[4] = {yuv_, nullptr, nullptr, nullptr};
    int dst_stride[4] = {bytesperline, 0, 0, 0};

    sws_scale(sws_, src, src_stride, 0, h, dst, dst_stride);

    ssize_t written = ::write(fd, yuv_, (size_t)sizeimage);
    if (written < 0) {
        err_ = "write failed";
        fprintf(stderr, "[v4l2] write failed: %s\n", strerror(errno));
    }
}
