#include "net.h"

#ifdef _WIN32
  #include <ws2tcpip.h>
  #define CLOSESOCK closesocket
#else
  #include <unistd.h>
  #include <arpa/inet.h>
  #include <sys/time.h>
  #define CLOSESOCK ::close
#endif

#include <cstring>

static void set_timeouts(socket_t s, unsigned ms) {
#ifdef _WIN32
  DWORD t = ms;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
#else
  timeval tv;
  tv.tv_sec = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

bool net_init() {
  static bool started = false;
  if (started) return true;
#ifdef _WIN32
  WSADATA wsa;
  started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
  return started;
#else
  started = true;
  return true;
#endif
}

socket_t net_connect_tcp(const char* host, uint16_t port) {
  socket_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET_VAL) return INVALID_SOCKET_VAL;

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    net_close(s);
    return INVALID_SOCKET_VAL;
  }

  set_timeouts(s, 1000);

  if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
    net_close(s);
    return INVALID_SOCKET_VAL;
  }
  return s;
}

void net_set_timeout(socket_t s, unsigned ms) {
  set_timeouts(s, ms);
}

bool net_send(socket_t s, const void* buf, size_t len) {
  const char* p = (const char*)buf;
  size_t sent = 0;
  while (sent < len) {
#ifdef _WIN32
    int n = send(s, p + sent, (int)(len - sent), 0);
#else
    ssize_t n = send(s, p + sent, len - sent, MSG_NOSIGNAL);
#endif
    if (n <= 0) return false;
    sent += (size_t)n;
  }
  return true;
}

bool net_recv_full(socket_t s, void* buf, size_t len) {
  char* p = (char*)buf;
  size_t got = 0;
  while (got < len) {
#ifdef _WIN32
    int n = recv(s, p + got, (int)(len - got), 0);
#else
    ssize_t n = recv(s, p + got, len - got, 0);
#endif
    if (n <= 0) return false;
    got += (size_t)n;
  }
  return true;
}

void net_close(socket_t s) {
  if (s != INVALID_SOCKET_VAL) CLOSESOCK(s);
}
