#ifndef UDP_SOCKET_H
#define UDP_SOCKET_H

#include "tcp_socket.h"

#include <cstdint>
#include <string>

// Windows UDP helpers (Winsock2). Separate from the Day 1 TCP path.

SocketHandle createUdpSocket();

// Bind to all IPv4 interfaces (INADDR_ANY) on port.
bool bindUdp(SocketHandle sock, uint16_t port);

// Bind to a specific IPv4 address. Use "127.0.0.1" for loopback-only.
// Port 0 asks Windows for an ephemeral local port.
bool bindUdpAddress(SocketHandle sock, const std::string& ipv4, uint16_t port);

// For senders: bind the socket to a local address that matches the destination
// so Windows does not pick a VPN/LAN source IP when talking to 127.0.0.1.
bool bindUdpForDestination(SocketHandle sock, const std::string& destHost);

bool getUdpLocalEndpoint(SocketHandle sock, std::string& ipv4, uint16_t& port);

// Receive timeout in milliseconds. 0 means wait forever.
bool setUdpRecvTimeout(SocketHandle sock, int timeout_ms);

bool sendUdp(SocketHandle sock,
             const unsigned char* data,
             int length,
             const std::string& host,
             uint16_t port);

// Returns bytes received, 0 on timeout, -1 on error.
int recvUdp(SocketHandle sock, unsigned char* buffer, int max_length);

// Same as recvUdp, plus the sender IPv4 address/port for ACK replies.
int recvUdpFrom(SocketHandle sock,
                unsigned char* buffer,
                int max_length,
                std::string& fromHost,
                uint16_t& fromPort);

#endif // UDP_SOCKET_H
