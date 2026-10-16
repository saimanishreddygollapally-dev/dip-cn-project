#include "image_processing.h"
#include "impair.h"
#include "packet.h"
#include "udp_socket.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe
        << " [port] [output_image] [timeout_ms] [max_transfers] [ack_loss_pct] [seed]\n"
        << "Defaults: 5002 results/received_reliable.jpg 20000 1 0 1\n";
}

int parseInt(const char* text, const char* name, int minValue, int maxValue) {
    int value = std::stoi(text);
    if (value < minValue || value > maxValue) {
        throw std::runtime_error(std::string("Invalid ") + name);
    }
    return value;
}

double parseDouble(const char* text, const char* name, double minValue, double maxValue) {
    double value = std::stod(text);
    if (value < minValue || value > maxValue) {
        throw std::runtime_error(std::string("Invalid ") + name);
    }
    return value;
}

bool sendCtrl(SocketHandle sock,
              uint8_t type,
              uint32_t transferId,
              uint32_t seq,
              uint32_t total,
              const std::string& host,
              uint16_t port,
              ImpairStats& stats) {
    std::vector<unsigned char> pkt = encodePacket(type, transferId, seq, total, 0, 0);
    return impairSendAck(sock, pkt, host, port, stats);
}

int countMissing(const std::vector<int>& have) {
    int n = 0;
    for (size_t i = 0; i < have.size(); ++i) {
        if (!have[i]) {
            n++;
        }
    }
    return n;
}

uint32_t firstMissing(const std::vector<int>& have) {
    for (uint32_t i = 0; i < have.size(); ++i) {
        if (!have[i]) {
            return i;
        }
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        printUsage(argv[0]);
        return 0;
    }

    uint16_t port = 5002;
    std::string outputPath = "results/received_reliable.jpg";
    int timeoutMs = 20000;
    int maxTransfers = 1;
    double ackLoss = 0.0;
    unsigned int seed = 1;

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
        if (argc >= 5) {
            maxTransfers = parseInt(argv[4], "max_transfers", 1, 20);
        }
        if (argc >= 6) {
            ackLoss = parseDouble(argv[5], "ack_loss_pct", 0.0, 100.0);
        }
        if (argc >= 7) {
            seed = static_cast<unsigned int>(parseInt(argv[6], "seed", 0, 2000000000));
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
    if (sock == INVALID_SOCKET || !bindUdp(sock, port) || !setUdpRecvTimeout(sock, 200)) {
        closeSocket(sock);
        netCleanup();
        return 1;
    }

    ImpairConfig impair;
    impair.loss_percent = 0.0;
    impair.delay_ms = 0;
    impair.jitter_ms = 0;
    impair.bandwidth_kbps = 0.0;
    impair.reorder_window = 0;
    impair.seed = seed;
    impair.ack_loss_percent = ackLoss;
    impairInit(impair);

    ImpairStats ackStats;
    ackStats.generated = 0;
    ackStats.sent = 0;
    ackStats.dropped = 0;
    ackStats.acks_dropped = 0;

    std::cout << "Reliable UDP server on port " << port
              << " (max_transfers=" << maxTransfers
              << ", ack_loss=" << ackLoss << "%)" << std::endl;

    const int kMax = PKT_HEADER_SIZE + PKT_MAX_PAYLOAD + 16;
    std::vector<unsigned char> recvBuf(kMax);
    int completedOk = 0;
    int lastStatus = 1;

    for (int t = 0; t < maxTransfers; ++t) {
        bool haveStart = false;
        bool saved = false;
        StartInfo startInfo;
        uint32_t transferId = 0;
        uint32_t totalPackets = 0;
        std::vector<int> haveSlot;
        std::vector<std::vector<unsigned char> > slots;
        std::string peer = "127.0.0.1";
        uint16_t peerPort = 0;
        int duplicates = 0;
        int rejected = 0;
        int dataStored = 0;
        int idleMs = 0;

        std::cout << "Waiting for transfer " << (t + 1) << " ..." << std::endl;

        while (idleMs < timeoutMs) {
            uint16_t fromPort = 0;
            std::string fromHost;
            int n = recvUdpFrom(sock, recvBuf.data(), kMax, fromHost, fromPort);
            if (n < 0) {
                break;
            }
            if (n == 0) {
                idleMs += 200;
                if (haveStart && countMissing(haveSlot) > 0 && !peer.empty()) {
                    sendCtrl(sock, PKT_NACK, transferId, firstMissing(haveSlot),
                             totalPackets, peer, peerPort, ackStats);
                }
                continue;
            }
            idleMs = 0;
            peer = fromHost;
            peerPort = fromPort;

            Packet pkt;
            if (!decodePacket(recvBuf.data(), n, pkt)) {
                rejected++;
                continue;
            }

            if (pkt.type == PKT_START) {
                if (pkt.total_packets == 0 || pkt.total_packets > 10000) {
                    rejected++;
                    continue;
                }
                if (!decodeStartPayload(pkt.payload, startInfo)) {
                    rejected++;
                    continue;
                }
                if (!haveStart || pkt.transfer_id != transferId) {
                    transferId = pkt.transfer_id;
                    totalPackets = pkt.total_packets;
                    haveSlot.assign(totalPackets, 0);
                    slots.assign(totalPackets, std::vector<unsigned char>());
                    dataStored = 0;
                    saved = false;
                    haveStart = true;
                    std::cout << "START transfer " << transferId
                              << " packets=" << totalPackets
                              << " jpeg=" << startInfo.jpeg_size << std::endl;
                }
                sendCtrl(sock, PKT_ACK, transferId, SEQ_ACK_START, totalPackets,
                         peer, peerPort, ackStats);
                continue;
            }

            if (!haveStart || pkt.transfer_id != transferId) {
                rejected++;
                continue;
            }

            if (pkt.type == PKT_DATA) {
                if (pkt.seq >= totalPackets) {
                    rejected++;
                    continue;
                }
                if (haveSlot[pkt.seq]) {
                    duplicates++;
                } else {
                    haveSlot[pkt.seq] = 1;
                    slots[pkt.seq] = pkt.payload;
                    dataStored++;
                }
                sendCtrl(sock, PKT_ACK, transferId, pkt.seq, totalPackets,
                         peer, peerPort, ackStats);
                // Do not NACK during the first burst: missing seqs are often
                // still in flight. NACKs are sent on idle timeout or END.
                continue;
            }

            if (pkt.type == PKT_END) {
                int missing = countMissing(haveSlot);
                if (missing > 0) {
                    std::cout << "END with " << missing << " missing packets" << std::endl;
                    sendCtrl(sock, PKT_NACK, transferId, firstMissing(haveSlot),
                             totalPackets, peer, peerPort, ackStats);
                    continue;
                }

                std::vector<unsigned char> jpeg;
                for (uint32_t i = 0; i < totalPackets; ++i) {
                    jpeg.insert(jpeg.end(), slots[i].begin(), slots[i].end());
                }
                if (jpeg.size() != startInfo.jpeg_size) {
                    std::cout << "Size mismatch, not saving." << std::endl;
                    lastStatus = 1;
                    break;
                }

                std::string outFile = outputPath;
                if (maxTransfers > 1) {
                    std::ostringstream oss;
                    oss << outputPath << "." << (t + 1) << ".jpg";
                    outFile = oss.str();
                }

                try {
                    cv::Mat img = decompressJpeg(jpeg);
                    if (!saved) {
                        if (!cv::imwrite(outFile, img)) {
                            std::cerr << "Failed to save " << outFile << std::endl;
                            lastStatus = 1;
                            break;
                        }
                        saved = true;
                        std::cout << "Saved " << outFile << " "
                                  << img.cols << "x" << img.rows
                                  << " dups=" << duplicates
                                  << " rejected=" << rejected
                                  << " stored=" << dataStored << std::endl;
                    }
                } catch (const std::exception& ex) {
                    std::cerr << ex.what() << std::endl;
                    lastStatus = 1;
                    break;
                }

                sendCtrl(sock, PKT_ACK, transferId, SEQ_ACK_END, totalPackets,
                         peer, peerPort, ackStats);
                completedOk++;
                lastStatus = 0;
                break;
            }
        }

        if (!saved) {
            std::cout << "Transfer " << (t + 1)
                      << " incomplete. Not saving a successful image." << std::endl;
            lastStatus = 1;
        }
    }

    std::cout << "ACK datagrams dropped by simulator: " << ackStats.acks_dropped << std::endl;
    std::cout << "Completed transfers: " << completedOk << "/" << maxTransfers << std::endl;
    closeSocket(sock);
    netCleanup();
    return (completedOk == maxTransfers) ? 0 : lastStatus;
}
