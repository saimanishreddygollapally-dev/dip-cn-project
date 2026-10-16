#include "image_processing.h"
#include "packet.h"
#include "udp_socket.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printUsage(const char* exe) {
    std::cout << "Usage:\n"
              << "  " << exe << " [port] [output_image] [timeout_ms]\n"
              << "Defaults: port=5001 output_image=results/received_udp.jpg timeout_ms=10000\n"
              << "The server waits for one UDP transfer, then exits.\n"
              << "timeout_ms is idle time with no datagrams (START/END/DATA).\n";
}

int parseInt(const char* text, const char* name, int minValue, int maxValue) {
    int value = std::stoi(text);
    if (value < minValue || value > maxValue) {
        throw std::runtime_error(std::string("Invalid ") + name);
    }
    return value;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        printUsage(argv[0]);
        return 0;
    }

    uint16_t port = 5001;
    std::string outputPath = "results/received_udp.jpg";
    int timeoutMs = 10000;

    try {
        if (argc >= 2) {
            port = static_cast<uint16_t>(parseInt(argv[1], "port", 1, 65535));
        }
        if (argc >= 3) {
            outputPath = argv[2];
        }
        if (argc >= 4) {
            timeoutMs = parseInt(argv[3], "timeout_ms", 200, 120000);
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    if (!netInit()) {
        return 1;
    }

    SocketHandle sock = createUdpSocket();
    if (sock == INVALID_SOCKET) {
        netCleanup();
        return 1;
    }
    if (!bindUdp(sock, port) || !setUdpRecvTimeout(sock, 200)) {
        closeSocket(sock);
        netCleanup();
        return 1;
    }

    std::string boundIp;
    uint16_t boundPort = 0;
    if (getUdpLocalEndpoint(sock, boundIp, boundPort)) {
        std::cout << "UDP server bound to " << boundIp << ":" << boundPort
                  << " (INADDR_ANY, all IPv4 interfaces)" << std::endl;
    }
    std::cout << "UDP server listening on port " << port << " ..." << std::endl;
    std::cout << "Idle timeout: " << timeoutMs << " ms" << std::endl;
    std::cout.flush();

    const int kMaxDatagram = PKT_HEADER_SIZE + PKT_MAX_PAYLOAD + 16;
    std::vector<unsigned char> recvBuf(kMaxDatagram);

    bool haveStart = false;
    bool haveEnd = false;
    StartInfo startInfo;
    uint32_t transferId = 0;
    uint32_t totalPackets = 0;
    std::vector<int> haveSlot;
    std::vector<std::vector<unsigned char> > slots;

    int datagramsReceived = 0;
    int packetsAccepted = 0;
    int duplicates = 0;
    int rejected = 0;
    int dataAccepted = 0;

    auto t0 = std::chrono::steady_clock::now();
    int idleMs = 0;

    while (idleMs < timeoutMs) {
        int n = recvUdp(sock, recvBuf.data(), static_cast<int>(recvBuf.size()));
        if (n < 0) {
            std::cerr << "recvfrom error" << std::endl;
            break;
        }
        if (n == 0) {
            idleMs += 200;
            continue;
        }

        idleMs = 0;
        datagramsReceived++;

        Packet pkt;
        if (!decodePacket(recvBuf.data(), n, pkt)) {
            rejected++;
            continue;
        }

        if (pkt.type == PKT_START) {
            if (haveStart && pkt.transfer_id != transferId) {
                rejected++;
                continue;
            }
            if (!decodeStartPayload(pkt.payload, startInfo)) {
                rejected++;
                continue;
            }
            if (pkt.total_packets == 0 || pkt.total_packets > 20000) {
                rejected++;
                continue;
            }
            transferId = pkt.transfer_id;
            totalPackets = pkt.total_packets;
            haveStart = true;
            haveSlot.assign(totalPackets, 0);
            slots.assign(totalPackets, std::vector<unsigned char>());
            packetsAccepted++;
            std::cout << "START received: transfer " << transferId
                      << ", data packets " << totalPackets
                      << ", jpeg_size " << startInfo.jpeg_size << std::endl;
            continue;
        }

        if (!haveStart) {
            rejected++;
            continue;
        }
        if (pkt.transfer_id != transferId || pkt.total_packets != totalPackets) {
            rejected++;
            continue;
        }

        if (pkt.type == PKT_END) {
            haveEnd = true;
            packetsAccepted++;
            std::cout << "END received." << std::endl;
            break;
        }

        if (pkt.type != PKT_DATA) {
            rejected++;
            continue;
        }
        if (pkt.seq >= totalPackets) {
            rejected++;
            continue;
        }

        packetsAccepted++;
        if (haveSlot[pkt.seq]) {
            duplicates++;
            continue;
        }
        haveSlot[pkt.seq] = 1;
        slots[pkt.seq] = pkt.payload;
        dataAccepted++;
    }

    auto t1 = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(t1 - t0).count();

    int missing = 0;
    if (haveStart) {
        for (uint32_t i = 0; i < totalPackets; ++i) {
            if (!haveSlot[i]) {
                missing++;
            }
        }
    }

    std::cout << "=== UDP Server Statistics ===" << std::endl;
    std::cout << "Datagrams received: " << datagramsReceived << std::endl;
    std::cout << "Packets accepted: " << packetsAccepted << std::endl;
    std::cout << "DATA packets stored: " << dataAccepted << std::endl;
    std::cout << "Duplicate packets: " << duplicates << std::endl;
    std::cout << "Rejected/corrupted/malformed: " << rejected << std::endl;
    std::cout << "Missing DATA packets: " << missing << std::endl;
    std::cout << "START seen: " << (haveStart ? "yes" : "no") << std::endl;
    std::cout << "END seen: " << (haveEnd ? "yes" : "no") << std::endl;
    std::cout << "Time: " << seconds << " s" << std::endl;

    if (!haveStart) {
        std::cout << "Transfer incomplete: no valid START packet." << std::endl;
        closeSocket(sock);
        netCleanup();
        return 1;
    }

    std::cout << "Metadata width x height: " << startInfo.width
              << " x " << startInfo.height << std::endl;
    std::cout << "JPEG quality: " << startInfo.jpeg_quality << std::endl;
    std::cout << "Expected JPEG bytes: " << startInfo.jpeg_size << std::endl;

    if (missing > 0) {
        std::cout << "Transfer incomplete. Not saving a reconstructed image." << std::endl;
        std::cout << "Missing sequence numbers:";
        int shown = 0;
        for (uint32_t i = 0; i < totalPackets && shown < 20; ++i) {
            if (!haveSlot[i]) {
                std::cout << " " << i;
                shown++;
            }
        }
        if (missing > 20) {
            std::cout << " ...";
        }
        std::cout << std::endl;
        closeSocket(sock);
        netCleanup();
        return 1;
    }

    std::vector<unsigned char> jpeg;
    jpeg.reserve(startInfo.jpeg_size);
    for (uint32_t i = 0; i < totalPackets; ++i) {
        jpeg.insert(jpeg.end(), slots[i].begin(), slots[i].end());
    }

    if (jpeg.size() != startInfo.jpeg_size) {
        std::cout << "Reassembled size " << jpeg.size()
                  << " does not match expected " << startInfo.jpeg_size << std::endl;
        closeSocket(sock);
        netCleanup();
        return 1;
    }

    double mbps = 0.0;
    if (seconds > 0.0) {
        mbps = (static_cast<double>(jpeg.size()) * 8.0) / (seconds * 1e6);
    }
    std::cout << "Reassembled JPEG bytes: " << jpeg.size() << std::endl;
    std::cout << "Approx throughput: " << mbps
              << " Mbps (localhost, not real WAN/Wi-Fi)" << std::endl;

    try {
        cv::Mat reconstructed = decompressJpeg(jpeg);
        if (!cv::imwrite(outputPath, reconstructed)) {
            std::cerr << "Failed to save " << outputPath << std::endl;
            closeSocket(sock);
            netCleanup();
            return 1;
        }
        std::cout << "Reconstructed image size: "
                  << reconstructed.cols << " x " << reconstructed.rows << std::endl;
        std::cout << "Saved reconstructed image to: " << outputPath << std::endl;
        std::cout << "UDP server finished successfully." << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Decode error: " << ex.what() << std::endl;
        closeSocket(sock);
        netCleanup();
        return 1;
    }

    closeSocket(sock);
    netCleanup();
    return 0;
}
