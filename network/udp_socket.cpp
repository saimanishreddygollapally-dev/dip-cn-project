#include "udp_socket.h"

#include <cstring>
#include <iostream>

namespace {

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

void printWsa(const char* operation) {
    std::cerr << operation << " failed, WSAGetLastError()=" << WSAGetLastError()
              << std::endl;
}

bool fillIpv4(sockaddr_in& addr, const std::string& ipv4, uint16_t port) {
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ipv4.empty() || ipv4 == "0.0.0.0" || ipv4 == "*") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        return true;
    }
    if (inet_pton(AF_INET, ipv4.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "inet_pton failed for " << ipv4
                  << ", WSAGetLastError()=" << WSAGetLastError() << std::endl;
        return false;
    }
    return true;
}

bool isLoopbackHost(const std::string& host) {
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

} // namespace

SocketHandle createUdpSocket() {
    SocketHandle sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printWsa("UDP socket()");
        return INVALID_SOCKET;
    }

    // Windows can report WSAECONNRESET (10054) on UDP recvfrom after a
    // previous send got ICMP port unreachable. Disable that so a localhost
    // receiver is not aborted by ICMP noise.
    BOOL disableReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(sock, SIO_UDP_CONNRESET, &disableReset, sizeof(disableReset),
             NULL, 0, &bytes, NULL, NULL);

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) != 0) {
        printWsa("setsockopt(SO_REUSEADDR)");
    }
    return sock;
}

bool bindUdp(SocketHandle sock, uint16_t port) {
    return bindUdpAddress(sock, "0.0.0.0", port);
}

bool bindUdpAddress(SocketHandle sock, const std::string& ipv4, uint16_t port) {
    sockaddr_in addr;
    if (!fillIpv4(addr, ipv4, port)) {
        return false;
    }
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "UDP bind() failed on " << ipv4 << ":" << port
                  << ", WSAGetLastError()=" << WSAGetLastError() << std::endl;
        return false;
    }
    return true;
}

bool getUdpLocalEndpoint(SocketHandle sock, std::string& ipv4, uint16_t& port) {
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
        printWsa("getsockname");
        return false;
    }
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    ipv4 = ip;
    port = ntohs(addr.sin_port);
    return true;
}

bool bindUdpForDestination(SocketHandle sock, const std::string& destHost) {
    sockaddr_in cur;
    int curLen = sizeof(cur);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&cur), &curLen) == 0 &&
        ntohs(cur.sin_port) != 0) {
        return true;
    }

    std::string localIp;
    uint16_t localPort = 0;

    // Loopback destinations must use a loopback source address. An unbound
    // UDP socket on Windows may pick a VPN/LAN IP; those packets to 127.0.0.1
    // are dropped even though sendto() returns success.
    const std::string bindIp = isLoopbackHost(destHost) ? "127.0.0.1" : "0.0.0.0";
    if (!bindUdpAddress(sock, bindIp, 0)) {
        return false;
    }
    if (getUdpLocalEndpoint(sock, localIp, localPort)) {
        std::cout << "UDP client bound to " << localIp << ":" << localPort
                  << std::endl;
    }
    return true;
}

bool setUdpRecvTimeout(SocketHandle sock, int timeout_ms) {
    DWORD ms = static_cast<DWORD>(timeout_ms);
    int result = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                            reinterpret_cast<const char*>(&ms), sizeof(ms));
    if (result != 0) {
        printWsa("setsockopt(SO_RCVTIMEO)");
        return false;
    }
    return true;
}

bool sendUdp(SocketHandle sock,
             const unsigned char* data,
             int length,
             const std::string& host,
             uint16_t port) {
    if (!bindUdpForDestination(sock, host)) {
        return false;
    }

    sockaddr_in addr;
    if (!fillIpv4(addr, host, port)) {
        return false;
    }

    int sent = sendto(sock, reinterpret_cast<const char*>(data), length, 0,
                      reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (sent != length) {
        std::cerr << "sendto() to " << host << ":" << port
                  << " sent " << sent << " of " << length
                  << " bytes, WSAGetLastError()=" << WSAGetLastError()
                  << std::endl;
        return false;
    }
    return true;
}

int recvUdp(SocketHandle sock, unsigned char* buffer, int max_length) {
    std::string host;
    uint16_t port = 0;
    return recvUdpFrom(sock, buffer, max_length, host, port);
}

int recvUdpFrom(SocketHandle sock,
                unsigned char* buffer,
                int max_length,
                std::string& fromHost,
                uint16_t& fromPort) {
    sockaddr_in from;
    int fromLen = sizeof(from);
    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), max_length, 0,
                     reinterpret_cast<sockaddr*>(&from), &fromLen);
    if (n >= 0) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        fromHost = ip;
        fromPort = ntohs(from.sin_port);
        return n;
    }
    int err = WSAGetLastError();
    if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
        return 0;
    }
    std::cerr << "recvfrom() failed, WSAGetLastError()=" << err << std::endl;
    return -1;
}
