#pragma once
#include <functional>
#include <vector>

#include "adb.h"
#include "cameras.h"
#include "session.h"

struct GuiHooks {
    std::function<std::vector<AdbDevice>()> scan_devices;
    std::function<bool(const std::string&)> connect_host;
    std::function<bool(const std::string&)> query_caps;
    std::function<void(const std::string& serial, const std::string& camera_id,
                       const std::string& size, int fps, bool high_speed, int bitrate)>
        start_stream;
};

void run_gui(Session* session, std::vector<CameraInfo>* cams, const GuiHooks& hooks);
