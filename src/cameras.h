#pragma once
#include <string>
#include <vector>

struct CameraSize {
    int w = 0, h = 0;
    bool high_speed = false;
    std::vector<int> fps;
};

struct CameraInfo {
    std::string id;
    std::string facing;
    float focal_mm = 0.f;
    int sensor_w = 0, sensor_h = 0;
    std::vector<int> fps;
    float zoom_min = 0.f, zoom_max = 0.f;
    bool has_zoom = false;

    float focus_min = 0.f;
    bool has_focus = false;
    int iso_min = 0, iso_max = 0;
    bool has_iso = false;
    long exp_min_ns = 0, exp_max_ns = 0;
    bool has_exp = false;

    std::vector<CameraSize> sizes;
    std::vector<CameraSize> high_speed_sizes;
};

bool parse_camera_list(const std::string& output, std::vector<CameraInfo>* out);

int max_fps(const CameraInfo& cam);
