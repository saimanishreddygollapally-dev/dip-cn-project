#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include "tcp_socket.h"

#include <cstdint>
#include <string>

// Windows UDP helpers (Winsock2). Separate from the Day 1 TCP path.

SocketHandle createUdpSocket();

bool bindUdp(SocketHandle sock, uint16_t port);

// Receive timeout in milliseconds. 0 means wait forever.
bool setUdpRecvTimeout(SocketHandle sock, int timeout_ms);

bool sendUdp(SocketHandle sock,
             const unsigned char* data,
             int length,
             const std::string& host,
             uint16_t port);

// Returns bytes received, 0 on timeout, -1 on error.
int recvUdp(SocketHandle sock, unsigned char* buffer, int max_length);

#endif // UDP_SOCKET_H
