#include "stream_session.h"
#include "tcp_socket.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe
        << " <image_path> [server_ip] [port] [payload] [loss_pct] [delay_ms] "
           "[jitter_ms] [bandwidth_kbps] [reorder_window] [seed] [mode] "
           "[rto_ms] [max_retries] [ack_loss_pct]\n"
        << "Defaults: 127.0.0.1 5002 1200  0 0 0 0 0 1  0  300 20 0\n"
        << "mode: 0=fixed  1=network-only  2=network+image-aware\n";
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
        ReliableConfig rcfg;
        rcfg.host = (argc >= 3) ? argv[2] : "127.0.0.1";
        rcfg.port = static_cast<uint16_t>((argc >= 4) ? parseInt(argv[3], "port", 1, 65535) : 5002);
        const int payload = (argc >= 5) ? parseInt(argv[4], "payload", 64, PKT_MAX_PAYLOAD) : 1200;

        ImpairConfig impair;
        impair.loss_percent = (argc >= 6) ? parseDouble(argv[5], "loss_pct", 0.0, 100.0) : 0.0;
        impair.delay_ms = (argc >= 7) ? parseInt(argv[6], "delay_ms", 0, 10000) : 0;
        impair.jitter_ms = (argc >= 8) ? parseInt(argv[7], "jitter_ms", 0, 10000) : 0;
        impair.bandwidth_kbps = (argc >= 9) ? parseDouble(argv[8], "bandwidth_kbps", 0.0, 1000000.0) : 0.0;
        impair.reorder_window = (argc >= 10) ? parseInt(argv[9], "reorder_window", 0, 64) : 0;
        impair.seed = (argc >= 11) ? static_cast<unsigned int>(parseInt(argv[10], "seed", 0, 2000000000)) : 1u;
        const int mode = (argc >= 12) ? parseInt(argv[11], "mode", 0, 2) : MODE_FIXED;
        rcfg.rto_ms = (argc >= 13) ? parseInt(argv[12], "rto_ms", 50, 5000) : 300;
        rcfg.max_retries = (argc >= 14) ? parseInt(argv[13], "max_retries", 0, 100) : 20;
        impair.ack_loss_percent = (argc >= 15) ? parseDouble(argv[14], "ack_loss_pct", 0.0, 100.0) : 0.0;
        rcfg.overall_timeout_ms = 30000;

        if (!netInit()) {
            return 1;
        }

        SessionResult session;
        bool ok = streamImage(imagePath, mode, impair, rcfg, payload, session);

        std::cout << "=== Reliable UDP Client ===" << std::endl;
        std::cout << "Success: " << (ok ? "yes" : "no") << std::endl;
        if (!ok) {
            std::cout << "Reason: " << session.fail_reason << std::endl;
        }
        std::cout << "Resolution: " << session.width << " x " << session.height << std::endl;
        std::cout << "JPEG quality: " << session.jpeg_quality << std::endl;
        std::cout << "JPEG bytes: " << session.jpeg_bytes << std::endl;
        std::cout << "DATA packets: " << session.data_packets << std::endl;
        std::cout << "Packets generated: " << session.net.packets_generated << std::endl;
        std::cout << "Packets sent: " << session.net.packets_sent << std::endl;
        std::cout << "Simulator DATA drops: " << session.net.packets_dropped << std::endl;
        std::cout << "ACKs received: " << session.net.acks_received << std::endl;
        std::cout << "NACKs received: " << session.net.nacks_received << std::endl;
        std::cout << "Retransmissions: " << session.retransmissions << std::endl;
        std::cout << "Retries exhausted: " << session.net.retries_exhausted << std::endl;
        std::cout << "Time: " << session.seconds << " s" << std::endl;
        std::cout << "Throughput: " << session.throughput_mbps
                  << " Mbps (localhost, not real WAN/Wi-Fi)" << std::endl;
        std::cout << "Configured bandwidth: " << impair.bandwidth_kbps << " kbps" << std::endl;
        std::cout << "Configured DATA loss: " << impair.loss_percent << " %" << std::endl;
        std::cout << "Configured ACK loss: " << impair.ack_loss_percent << " %" << std::endl;
        if (ok) {
            std::cout << "MSE:  " << session.mse << std::endl;
            std::cout << "PSNR: " << session.psnr << " dB" << std::endl;
            std::cout << "SSIM: " << session.ssim << std::endl;
        }

        netCleanup();
        return ok ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "client_reliable error: " << ex.what() << std::endl;
        printUsage(argv[0]);
        netCleanup();
        return 1;
    }
}
