#include "udp_socket.h"

#include <cstring>
#include <iostream>

SocketHandle createUdpSocket() {
    SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "UDP socket() failed" << std::endl;
        return INVALID_SOCKET;
    }
    return sock;
}

bool bindUdp(SocketHandle sock, uint16_t port) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "UDP bind() failed on port " << port << std::endl;
        return false;
    }
    return true;
}

bool setUdpRecvTimeout(SocketHandle sock, int timeout_ms) {
    DWORD ms = static_cast<DWORD>(timeout_ms);
    int result = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                            reinterpret_cast<const char*>(&ms), sizeof(ms));
    if (result != 0) {
        std::cerr << "setsockopt(SO_RCVTIMEO) failed" << std::endl;
        return false;
    }
    return true;
}

bool sendUdp(SocketHandle sock,
             const unsigned char* data,
             int length,
             const std::string& host,
             uint16_t port) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "Invalid UDP host address: " << host << std::endl;
        return false;
    }

    int sent = sendto(sock, reinterpret_cast<const char*>(data), length, 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == length;
}

int recvUdp(SocketHandle sock, unsigned char* buffer, int max_length) {
    sockaddr_in from;
    int fromLen = sizeof(from);
    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), max_length, 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n >= 0) {
        return n;
    }
    int err = WSAGetLastError();
    if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
        return 0;
    }
    return -1;
}
