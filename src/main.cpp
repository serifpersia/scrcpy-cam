
#include <cstdio>
#include <string>
#include <vector>

#include "adb.h"
#include "gui.h"
#include "session.h"

#ifndef DEFAULT_SERVER_APK
  #define DEFAULT_SERVER_APK "scrcpy-server"
#endif

int main() {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    Session session;
    std::vector<CameraInfo> cams;

    session.init_output();

    GuiHooks hooks;
    hooks.scan_devices = [] { return adb_list_devices(); };

    hooks.connect_host = [](const std::string& host) {
        printf("[adb] connect %s\n", host.c_str());
        return adb_connect(host) == 0;
    };

    hooks.query_caps = [&](const std::string& serial) {
        std::string err;
        cams.clear();
        if (!query_cameras(serial, DEFAULT_SERVER_APK, &cams, &err)) {
            fprintf(stderr, "[caps] %s\n", err.c_str());
            return false;
        }
        printf("[caps] %zu camera(s) on %s\n", cams.size(), serial.c_str());
        for (const CameraInfo& c : cams) {
            printf("  camera %s %s %dx%d maxfps=%d sizes=%zu highspeed=%zu\n", c.id.c_str(),
                   c.facing.c_str(), c.sensor_w, c.sensor_h, max_fps(c), c.sizes.size(),
                   c.high_speed_sizes.size());
        }
        return true;
    };

    hooks.start_stream = [&](const std::string& serial, const std::string& camera_id,
                             const std::string& size, int fps, bool high_speed,
                             int bitrate) {
        session.stop();
        session.cfg = SessionConfig{};
        session.cfg.serial = serial;
        session.cfg.server_apk = DEFAULT_SERVER_APK;
        session.cfg.camera_id = camera_id;
        session.cfg.camera_size = size;
        session.cfg.camera_fps = fps;
        session.cfg.video_bit_rate = bitrate;
        session.cfg.high_speed = high_speed;

        std::string err;
        if (!session.start(&err)) {
            fprintf(stderr, "[session] %s\n", err.c_str());
        } else {
            printf("[session] streaming from %s (%s, %d fps, %d bps)\n", serial.c_str(),
                   size.c_str(), fps, bitrate);
        }
    };

    run_gui(&session, &cams, hooks);
    session.shutdown_output();

#ifdef _WIN32
    CoUninitialize();
#endif
    return 0;
}
