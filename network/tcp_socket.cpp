#include "tcp_socket.h"

#include <cstring>
#include <iostream>

bool netInit() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return false;
    }
    return true;
}

void netCleanup() {
    WSACleanup();
}

void closeSocket(SocketHandle sock) {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
}

bool sendAll(SocketHandle sock, const char* data, int length) {
    int totalSent = 0;
    while (totalSent < length) {
        int sent = send(sock, data + totalSent, length - totalSent, 0);
        if (sent <= 0) {
            return false;
        }
        totalSent += sent;
    }
    return true;
}

bool receiveAll(SocketHandle sock, char* data, int length) {
    int totalReceived = 0;
    while (totalReceived < length) {
        int received = recv(sock, data + totalReceived, length - totalReceived, 0);
        if (received <= 0) {
            return false;
        }
        totalReceived += received;
    }
    return true;
}

bool sendUint32(SocketHandle sock, uint32_t value) {
    uint32_t netValue = htonl(value);
    return sendAll(sock, reinterpret_cast<const char*>(&netValue), sizeof(netValue));
}

bool receiveUint32(SocketHandle sock, uint32_t& value) {
    uint32_t netValue = 0;
    if (!receiveAll(sock, reinterpret_cast<char*>(&netValue), sizeof(netValue))) {
        return false;
    }
    value = ntohl(netValue);
    return true;
}

bool sendBuffer(SocketHandle sock, const std::vector<unsigned char>& buffer) {
    if (!sendUint32(sock, static_cast<uint32_t>(buffer.size()))) {
        return false;
    }
    if (buffer.empty()) {
        return true;
    }
    return sendAll(sock, reinterpret_cast<const char*>(buffer.data()),
                   static_cast<int>(buffer.size()));
}

bool receiveBuffer(SocketHandle sock, std::vector<unsigned char>& buffer) {
    uint32_t size = 0;
    if (!receiveUint32(sock, size)) {
        return false;
    }

    buffer.resize(size);
    if (size == 0) {
        return true;
    }
    return receiveAll(sock, reinterpret_cast<char*>(buffer.data()), static_cast<int>(size));
}

SocketHandle createTcpServer(uint16_t port, int backlog) {
    SocketHandle serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock == INVALID_SOCKET) {
        std::cerr << "socket() failed" << std::endl;
        return INVALID_SOCKET;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(serverSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind() failed on port " << port << std::endl;
        closeSocket(serverSock);
        return INVALID_SOCKET;
    }

    if (listen(serverSock, backlog) != 0) {
        std::cerr << "listen() failed" << std::endl;
        closeSocket(serverSock);
        return INVALID_SOCKET;
    }

    return serverSock;
}

SocketHandle acceptClient(SocketHandle serverSock) {
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    SocketHandle clientSock = accept(serverSock,
                                     reinterpret_cast<sockaddr*>(&clientAddr),
                                     &addrLen);
    return clientSock;
}

SocketHandle connectTcpClient(const std::string& host, uint16_t port) {
    SocketHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket() failed" << std::endl;
        return INVALID_SOCKET;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "Invalid host address: " << host << std::endl;
        closeSocket(sock);
        return INVALID_SOCKET;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "connect() failed to " << host << ":" << port << std::endl;
        closeSocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}
