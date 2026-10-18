#include "adb.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#else
  #include <sys/wait.h>
#endif

static std::string g_serial;

#ifdef _WIN32
  #define POPEN _popen
  #define PCLOSE _pclose
#else
  #define POPEN popen
  #define PCLOSE pclose
#endif

std::string adb_path() {
  const char* env = getenv("ADB");
  return env && *env ? std::string(env) : std::string("adb");
}

void adb_set_serial(const std::string& serial) {
  g_serial = serial;
}

const std::string& adb_serial() {
  return g_serial;
}

static std::string prefix() {
  std::string cmd = "\"" + adb_path() + "\"";
  if (!g_serial.empty()) {
    cmd += " -s \"" + g_serial + "\"";
  }
  return cmd;
}

int adb_run(const std::string& args) {
  std::string cmd = prefix() + " " + args;
#ifdef _WIN32
  cmd = "\"" + cmd + "\"";
  return std::system(cmd.c_str());
#else
  int status = std::system(cmd.c_str());
  if (status == -1) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

bool adb_capture(const std::string& args, std::string* out) {
  std::string cmd = prefix() + " " + args;
#ifdef _WIN32
  cmd = "\"" + cmd + "\"";
#endif
  FILE* f = POPEN(cmd.c_str(), "r");
  if (!f) return false;

  out->clear();
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
  return PCLOSE(f) != -1;
}


std::vector<AdbDevice> adb_list_devices() {
  std::vector<AdbDevice> devices;
  std::string out;
  std::string saved = g_serial;
  g_serial.clear();
  bool ok = adb_capture("devices -l", &out);
  g_serial = saved;
  if (!ok) return devices;

  size_t pos = 0;
  bool header = true;
  while (pos < out.size()) {
    size_t eol = out.find('\n', pos);
    std::string line = out.substr(pos, eol == std::string::npos ? eol : eol - pos);
    pos = (eol == std::string::npos) ? out.size() : eol + 1;

    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty()) continue;
    if (header) {
      header = false;
      continue;
    }
    if (line[0] == '*' || line.find("adb server") != std::string::npos) continue;

    AdbDevice d;
    size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos) continue;
    d.serial = line.substr(0, sp);
    size_t st = line.find_first_not_of(" \t", sp);
    if (st == std::string::npos) continue;
    d.state = line.substr(st, line.find(' ', st) - st);
    devices.push_back(d);
  }
  return devices;
}

int adb_connect(const std::string& host) {
  return adb_run("connect " + host);
}

int adb_disconnect(const std::string& host) {
  return host.empty() ? adb_run("disconnect") : adb_run("disconnect " + host);
}

bool adb_push(const std::string& local, const std::string& remote) {
  printf("* adb push %s -> %s\n", local.c_str(), remote.c_str());
  return adb_run("push \"" + local + "\" " + remote) == 0;
}

bool adb_forward(uint16_t port, const std::string& socket_name) {
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
  printf("* adb forward tcp:%s localabstract:%s\n", port_str, socket_name.c_str());
  return adb_run(std::string("forward tcp:") + port_str + " localabstract:" + socket_name) == 0;
}

bool adb_forward_remove(uint16_t port) {
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
  return adb_run(std::string("forward --remove tcp:") + port_str) == 0;
}

bool adb_spawn_detached(const std::string& args, const std::string& log_path) {
  std::string cmd = prefix() + " " + args;

#ifdef _WIN32
  HANDLE log = CreateFileA(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (log == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Could not open %s\n", log_path.c_str());
    return false;
  }

  STARTUPINFOA si;
  std::memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = log;
  si.hStdError = log;

  std::vector<char> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back('\0');

  PROCESS_INFORMATION pi;
  BOOL ok = CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
                            NULL, NULL, &si, &pi);
  CloseHandle(log);
  if (!ok) {
    fprintf(stderr, "Could not start adb (%lu)\n", GetLastError());
    return false;
  }

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return true;
#else
  std::string full = cmd + " > " + log_path + " 2>&1 &";
  return std::system(full.c_str()) != -1;
#endif
}
