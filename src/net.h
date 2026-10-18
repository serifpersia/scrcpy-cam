#pragma once
#include <cstddef>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
  typedef SOCKET socket_t;
  static const socket_t INVALID_SOCKET_VAL = INVALID_SOCKET;
#else
  #include <sys/socket.h>
  typedef int socket_t;
  static const socket_t INVALID_SOCKET_VAL = -1;
#endif

bool net_init();
socket_t net_connect_tcp(const char* host, uint16_t port);
void net_set_timeout(socket_t s, unsigned ms);
bool net_send(socket_t s, const void* buf, size_t len);
bool net_recv_full(socket_t s, void* buf, size_t len);
void net_close(socket_t s);
