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
              << "  " << exe << " [port] [output_path]\n"
              << "Defaults: port=5000 output_path=results/received.jpg\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const uint16_t port = static_cast<uint16_t>((argc >= 2) ? std::stoi(argv[1]) : 5000);
    const std::string outputPath = (argc >= 3) ? argv[2] : "results/received.jpg";

    if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        printUsage(argv[0]);
        return 0;
    }

    if (!netInit()) {
        return 1;
    }

    SocketHandle serverSock = createTcpServer(port);
    if (serverSock == INVALID_SOCKET) {
        netCleanup();
        return 1;
    }

    std::cout << "TCP server listening on port " << port << " ..." << std::endl;

    SocketHandle clientSock = acceptClient(serverSock);
    if (clientSock == INVALID_SOCKET) {
        std::cerr << "accept() failed" << std::endl;
        closeSocket(serverSock);
        netCleanup();
        return 1;
    }

    std::cout << "Client connected." << std::endl;

    try {
        auto t0 = std::chrono::steady_clock::now();

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t quality = 0;
        if (!receiveUint32(clientSock, width) ||
            !receiveUint32(clientSock, height) ||
            !receiveUint32(clientSock, quality)) {
            std::cerr << "Failed to receive metadata" << std::endl;
            closeSocket(clientSock);
            closeSocket(serverSock);
            netCleanup();
            return 1;
        }

        std::vector<unsigned char> jpegBuffer;
        if (!receiveBuffer(clientSock, jpegBuffer)) {
            std::cerr << "Failed to receive image data" << std::endl;
            closeSocket(clientSock);
            closeSocket(serverSock);
            netCleanup();
            return 1;
        }

        auto t1 = std::chrono::steady_clock::now();
        double seconds = std::chrono::duration<double>(t1 - t0).count();
        double mbps = (seconds > 0.0)
                          ? (static_cast<double>(jpegBuffer.size()) * 8.0) / (seconds * 1e6)
                          : 0.0;

        std::cout << "=== Received Metadata ===" << std::endl;
        std::cout << "Width: " << width << std::endl;
        std::cout << "Height: " << height << std::endl;
        std::cout << "JPEG quality: " << quality << std::endl;
        std::cout << "JPEG bytes: " << jpegBuffer.size() << std::endl;

        // Reconstruct / decode image
        cv::Mat reconstructed = decompressJpeg(jpegBuffer);
        if (!cv::imwrite(outputPath, reconstructed)) {
            std::cerr << "Failed to save image to " << outputPath << std::endl;
            closeSocket(clientSock);
            closeSocket(serverSock);
            netCleanup();
            return 1;
        }

        std::cout << "Reconstructed image size: "
                  << reconstructed.cols << " x " << reconstructed.rows << std::endl;
        std::cout << "Saved reconstructed image to: " << outputPath << std::endl;

        std::cout << "=== Transfer Statistics (server) ===" << std::endl;
        std::cout << "Bytes received: " << jpegBuffer.size() << std::endl;
        std::cout << "Time: " << seconds << " s" << std::endl;
        std::cout << "Throughput: " << mbps << " Mbps" << std::endl;

        closeSocket(clientSock);
        closeSocket(serverSock);
        netCleanup();
        std::cout << "Server finished successfully." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Server error: " << ex.what() << std::endl;
        closeSocket(clientSock);
        closeSocket(serverSock);
        netCleanup();
        return 1;
    }
}
