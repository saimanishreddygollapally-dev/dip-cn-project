#include "packet.h"
#include "udp_socket.h"

#include <iostream>
#include <string>
#include <vector>

// Sends malformed UDP datagrams so Day 2 Test 5 can check rejection.

int main(int argc, char* argv[]) {
    const std::string host = (argc >= 2) ? argv[1] : "127.0.0.1";
    const uint16_t port = static_cast<uint16_t>((argc >= 3) ? std::stoi(argv[2]) : 5001);

    if (!netInit()) {
        return 1;
    }
    SocketHandle sock = createUdpSocket();
    if (sock == INVALID_SOCKET) {
        netCleanup();
        return 1;
    }

    unsigned char tiny[4] = {1, 2, 3, 4};
    if (!sendUdp(sock, tiny, 4, host, port)) {
        std::cerr << "Failed to send short datagram" << std::endl;
    } else {
        std::cout << "Sent 4-byte malformed datagram" << std::endl;
    }

    unsigned char badMagic[PKT_HEADER_SIZE];
    for (int i = 0; i < PKT_HEADER_SIZE; ++i) {
        badMagic[i] = 0;
    }
    if (!sendUdp(sock, badMagic, PKT_HEADER_SIZE, host, port)) {
        std::cerr << "Failed to send bad-magic datagram" << std::endl;
    } else {
        std::cout << "Sent bad-magic header" << std::endl;
    }

    unsigned char payload[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    std::vector<unsigned char> good = encodePacket(
        PKT_DATA, 1, 0, 1, payload, 8);
    good[23] = static_cast<unsigned char>(good[23] ^ 0xFF);
    if (!sendUdp(sock, good.data(), static_cast<int>(good.size()), host, port)) {
        std::cerr << "Failed to send bad-checksum datagram" << std::endl;
    } else {
        std::cout << "Sent DATA packet with invalid checksum" << std::endl;
    }

    closeSocket(sock);
    netCleanup();
    std::cout << "send_bad_udp finished." << std::endl;
    return 0;
}
