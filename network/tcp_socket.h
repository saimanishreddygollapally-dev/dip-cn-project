#ifndef TCP_SOCKET_H
#define TCP_SOCKET_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <string>
#include <vector>

typedef SOCKET SocketHandle;

// Initialize Winsock. Safe to call once at program start.
bool netInit();

// Cleanup Winsock.
void netCleanup();

// Close a socket handle.
void closeSocket(SocketHandle sock);

// Send exactly 'length' bytes (handles partial sends).
bool sendAll(SocketHandle sock, const char* data, int length);

// Receive exactly 'length' bytes (handles partial receives).
bool receiveAll(SocketHandle sock, char* data, int length);

// Helpers for sending/receiving a 32-bit length prefix (network byte order).
bool sendUint32(SocketHandle sock, uint32_t value);
bool receiveUint32(SocketHandle sock, uint32_t& value);

// Send a byte buffer: first 4-byte size, then payload.
bool sendBuffer(SocketHandle sock, const std::vector<unsigned char>& buffer);

// Receive a byte buffer: read 4-byte size, then that many bytes.
bool receiveBuffer(SocketHandle sock, std::vector<unsigned char>& buffer);

// Create a TCP server listening socket bound to port.
SocketHandle createTcpServer(uint16_t port, int backlog = 1);

// Accept one client connection.
SocketHandle acceptClient(SocketHandle serverSock);

// Connect a TCP client to host:port.
SocketHandle connectTcpClient(const std::string& host, uint16_t port);

#endif // TCP_SOCKET_H
