#pragma once
#include <cstdint>
#include <string>
#include <vector>

std::string adb_path();

void adb_set_serial(const std::string& serial);
const std::string& adb_serial();

int adb_run(const std::string& args);

bool adb_capture(const std::string& args, std::string* out);


struct AdbDevice {
    std::string serial;
    std::string state;
    bool online() const { return state == "device"; }
};

std::vector<AdbDevice> adb_list_devices();

int adb_connect(const std::string& host);

int adb_disconnect(const std::string& host);

bool adb_push(const std::string& local, const std::string& remote);
bool adb_forward(uint16_t port, const std::string& socket_name);
bool adb_forward_remove(uint16_t port);

bool adb_spawn_detached(const std::string& args, const std::string& log_path);
