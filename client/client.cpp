#include "image_processing.h"
#include "tcp_socket.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* exe) {
    std::cout << "Usage:\n"
              << "  " << exe << " <image_path> [host] [port] [width] [height] [jpeg_quality]\n"
              << "Defaults: host=127.0.0.1 port=5000 width=640 height=480 jpeg_quality=75\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const std::string imagePath = argv[1];
    const std::string host = (argc >= 3) ? argv[2] : "127.0.0.1";
    const uint16_t port = static_cast<uint16_t>((argc >= 4) ? std::stoi(argv[3]) : 5000);
    const int outWidth = (argc >= 5) ? std::stoi(argv[4]) : 640;
    const int outHeight = (argc >= 6) ? std::stoi(argv[5]) : 480;
    const int jpegQuality = (argc >= 7) ? std::stoi(argv[6]) : 75;

    try {
        // --- DIP pipeline ---
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
        std::cout << "JPEG compressed size: " << jpegBuffer.size()
                  << " bytes (quality=" << jpegQuality << ")" << std::endl;

        // Local quality check (resized original vs JPEG round-trip)
        cv::Mat localDecoded = decompressJpeg(jpegBuffer);
        double mse = computeMSE(resized, localDecoded);
        double psnr = computePSNR(mse);
        std::cout << "Local MSE:  " << mse << std::endl;
        std::cout << "Local PSNR: " << psnr << " dB" << std::endl;

        // --- TCP send ---
        if (!netInit()) {
            return 1;
        }

        SocketHandle sock = connectTcpClient(host, port);
        if (sock == INVALID_SOCKET) {
            netCleanup();
            return 1;
        }

        std::cout << "Connected to " << host << ":" << port << std::endl;

        // Simple Day-1 metadata: width, height, quality, then JPEG bytes with size prefix.
        auto t0 = std::chrono::steady_clock::now();

        if (!sendUint32(sock, static_cast<uint32_t>(outWidth)) ||
            !sendUint32(sock, static_cast<uint32_t>(outHeight)) ||
            !sendUint32(sock, static_cast<uint32_t>(jpegQuality))) {
            std::cerr << "Failed to send metadata" << std::endl;
            closeSocket(sock);
            netCleanup();
            return 1;
        }

        if (!sendBuffer(sock, jpegBuffer)) {
            std::cerr << "Failed to send image data" << std::endl;
            closeSocket(sock);
            netCleanup();
            return 1;
        }

        auto t1 = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(t1 - t0).count();
        double mbps = (seconds > 0.0)
                          ? (static_cast<double>(jpegBuffer.size()) * 8.0) / (seconds * 1e6)
                          : 0.0;

        std::cout << "=== Transfer Statistics (client) ===" << std::endl;
        std::cout << "Bytes sent: " << jpegBuffer.size() << std::endl;
        std::cout << "Time: " << seconds << " s" << std::endl;
        std::cout << "Throughput: " << mbps << " Mbps" << std::endl;

        closeSocket(sock);
        netCleanup();
        std::cout << "Client finished successfully." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Client error: " << ex.what() << std::endl;
        netCleanup();
        return 1;
    }
}
