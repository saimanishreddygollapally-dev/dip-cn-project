#include "image_processing.h"
#include "impair.h"
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
    std::cout
        << "Usage:\n"
        << "  " << exe
        << " <image_path> [server_ip] [port] [payload_size] [loss_pct] "
           "[delay_ms] [jitter_ms] [bandwidth_kbps] [reorder_window] [seed]\n"
        << "Defaults: 127.0.0.1 5001 1200 0 0 0 0 0 1\n"
        << "Image is resized to 640x480 JPEG quality 75 (same as Day 1).\n"
        << "loss_pct is 0-100. bandwidth_kbps 0 means unlimited.\n"
        << "reorder_window >= 2 enables DATA packet reordering.\n";
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

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    try {
        const std::string imagePath = argv[1];
        const std::string host = (argc >= 3) ? argv[2] : "127.0.0.1";
        const uint16_t port = static_cast<uint16_t>((argc >= 4) ? parseInt(argv[3], "port", 1, 65535) : 5001);
        const int payloadSize = (argc >= 5) ? parseInt(argv[4], "payload_size", 64, PKT_MAX_PAYLOAD) : 1200;

        ImpairConfig impair;
        impair.loss_percent = (argc >= 6) ? parseDouble(argv[5], "loss_pct", 0.0, 100.0) : 0.0;
        impair.delay_ms = (argc >= 7) ? parseInt(argv[6], "delay_ms", 0, 10000) : 0;
        impair.jitter_ms = (argc >= 8) ? parseInt(argv[7], "jitter_ms", 0, 10000) : 0;
        impair.bandwidth_kbps = (argc >= 9) ? parseDouble(argv[8], "bandwidth_kbps", 0.0, 1000000.0) : 0.0;
        impair.reorder_window = (argc >= 10) ? parseInt(argv[9], "reorder_window", 0, 64) : 0;
        impair.seed = (argc >= 11) ? static_cast<unsigned int>(parseInt(argv[10], "seed", 0, 2000000000)) : 1u;

        const int outWidth = 640;
        const int outHeight = 480;
        const int jpegQuality = 75;

        cv::Mat original = loadImage(imagePath);
        std::cout << "Loaded image: " << imagePath
                  << " (" << original.cols << "x" << original.rows << ")" << std::endl;

        cv::Mat gray = toGrayscale(original);
        int histogram[256];
        computeHistogram(gray, histogram);
        double entropy = computeEntropy(histogram);
        cv::Mat blurred = applyGaussianFilter(gray, 5, 1.0);
        cv::Mat edges = detectEdgesSobel(blurred);
        double edgeDensity = computeEdgeDensity(edges, 50);
        printDipSummary(gray, histogram, entropy, edgeDensity);

        cv::Mat resized = resizeImage(original, outWidth, outHeight);
        std::vector<uchar> jpegBuffer;
        if (!compressToJpeg(resized, jpegBuffer, jpegQuality)) {
            std::cerr << "JPEG compression failed" << std::endl;
            return 1;
        }

        cv::Mat localDecoded = decompressJpeg(jpegBuffer);
        double mse = computeMSE(resized, localDecoded);
        double psnr = computePSNR(mse);
        std::cout << "JPEG compressed size: " << jpegBuffer.size()
                  << " bytes (quality=" << jpegQuality << ")" << std::endl;
        std::cout << "Local MSE:  " << mse << std::endl;
        std::cout << "Local PSNR: " << psnr << " dB" << std::endl;

        std::vector<std::vector<unsigned char> > parts;
        splitPayloads(jpegBuffer, payloadSize, parts);
        uint32_t totalPackets = static_cast<uint32_t>(parts.size());
        uint32_t transferId = static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);

        if (!netInit()) {
            return 1;
        }
        SocketHandle sock = createUdpSocket();
        if (sock == INVALID_SOCKET) {
            netCleanup();
            return 1;
        }

        impairInit(impair);
        ImpairStats stats;
        stats.generated = 0;
        stats.sent = 0;
        stats.dropped = 0;

        StartInfo startInfo;
        startInfo.width = static_cast<uint32_t>(outWidth);
        startInfo.height = static_cast<uint32_t>(outHeight);
        startInfo.jpeg_quality = static_cast<uint32_t>(jpegQuality);
        startInfo.jpeg_size = static_cast<uint32_t>(jpegBuffer.size());
        std::vector<unsigned char> startPayload = encodeStartPayload(startInfo);
        std::vector<unsigned char> startPkt = encodePacket(
            PKT_START, transferId, 0, totalPackets,
            startPayload.data(), static_cast<int>(startPayload.size()));

        auto t0 = std::chrono::steady_clock::now();

        if (!impairSend(sock, startPkt, host, port, stats)) {
            std::cerr << "Failed to send START" << std::endl;
            closeSocket(sock);
            netCleanup();
            return 1;
        }

        for (uint32_t i = 0; i < totalPackets; ++i) {
            std::vector<unsigned char> dataPkt = encodePacket(
                PKT_DATA, transferId, i, totalPackets,
                parts[i].data(), static_cast<int>(parts[i].size()));
            if (!impairSendData(sock, dataPkt, host, port, stats)) {
                std::cerr << "Failed to send DATA packet " << i << std::endl;
                closeSocket(sock);
                netCleanup();
                return 1;
            }
        }

        if (!impairFlush(sock, host, port, stats)) {
            std::cerr << "Failed to flush reorder buffer" << std::endl;
            closeSocket(sock);
            netCleanup();
            return 1;
        }

        std::vector<unsigned char> endPkt = encodePacket(
            PKT_END, transferId, totalPackets, totalPackets, 0, 0);
        if (!impairSend(sock, endPkt, host, port, stats)) {
            std::cerr << "Failed to send END" << std::endl;
            closeSocket(sock);
            netCleanup();
            return 1;
        }

        auto t1 = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(t1 - t0).count();
        double mbps = 0.0;
        if (seconds > 0.0 && stats.sent > 0) {
            mbps = (static_cast<double>(jpegBuffer.size()) * 8.0) / (seconds * 1e6);
        }

        std::cout << "=== UDP Client Statistics ===" << std::endl;
        std::cout << "Original size: " << original.cols << " x " << original.rows << std::endl;
        std::cout << "Sent frame size: " << outWidth << " x " << outHeight << std::endl;
        std::cout << "JPEG bytes: " << jpegBuffer.size() << std::endl;
        std::cout << "Transfer ID: " << transferId << std::endl;
        std::cout << "Payload size: " << payloadSize << " bytes" << std::endl;
        std::cout << "Packets generated: " << stats.generated << std::endl;
        std::cout << "Packets sent: " << stats.sent << " (includes START/END)" << std::endl;
        std::cout << "Packets dropped by simulator: " << stats.dropped << std::endl;
        std::cout << "Configured loss: " << impair.loss_percent << " %" << std::endl;
        std::cout << "Configured delay: " << impair.delay_ms << " ms" << std::endl;
        std::cout << "Configured jitter: " << impair.jitter_ms << " ms" << std::endl;
        std::cout << "Configured bandwidth: " << impair.bandwidth_kbps
                  << " kbps (0 = unlimited)" << std::endl;
        std::cout << "Configured reorder window: " << impair.reorder_window << std::endl;
        std::cout << "Time: " << seconds << " s" << std::endl;
        std::cout << "Approx throughput: " << mbps
                  << " Mbps (localhost, not real WAN/Wi-Fi)" << std::endl;

        closeSocket(sock);
        netCleanup();
        std::cout << "UDP client finished." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "UDP client error: " << ex.what() << std::endl;
        printUsage(argv[0]);
        netCleanup();
        return 1;
    }
}
