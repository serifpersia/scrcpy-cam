#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

struct VideoDecoder {
    bool inited = false;

    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;

    std::vector<uint8_t> rgb;
    int out_w = 0;
    int out_h = 0;

    std::vector<uint8_t> extradata;

    void reset();
    bool init_internal();
    int feed_config(const uint8_t* data, size_t size, int64_t pts);
    int decode(const uint8_t* data, size_t size, int64_t pts);
};
