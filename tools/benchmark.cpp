#include "stream_session.h"
#include "tcp_socket.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printUsage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe
        << " <image_path> [server_ip] [port] [loss_pct] [bandwidth_kbps] "
           "[reorder_window] [seed] [ack_loss_pct] [csv_path]\n"
        << "Runs fixed, network-only, and image-aware strategies.\n"
        << "Start server_reliable with max_transfers >= 3 first.\n";
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

const char* modeName(int mode) {
    if (mode == MODE_NETWORK) {
        return "network-only";
    }
    if (mode == MODE_IMAGE) {
        return "network+image";
    }
    return "fixed";
}

void writeRow(std::ostream& out, int mode, const SessionResult& s) {
    out << modeName(mode) << ","
        << s.width << "x" << s.height << ","
        << s.jpeg_quality << ","
        << s.jpeg_bytes << ","
        << s.data_packets << ","
        << s.seconds << ","
        << s.throughput_mbps << ","
        << s.retransmissions << ","
        << s.mse << ","
        << s.psnr << ","
        << s.ssim << ","
        << (s.success ? "success" : "failure")
        << "\n";
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
        ImpairConfig impair;
        impair.loss_percent = (argc >= 5) ? parseDouble(argv[4], "loss_pct", 0.0, 100.0) : 10.0;
        impair.delay_ms = 0;
        impair.jitter_ms = 0;
        impair.bandwidth_kbps = (argc >= 6) ? parseDouble(argv[5], "bandwidth_kbps", 0.0, 1000000.0) : 200.0;
        impair.reorder_window = (argc >= 7) ? parseInt(argv[6], "reorder_window", 0, 64) : 0;
        impair.seed = (argc >= 8) ? static_cast<unsigned int>(parseInt(argv[7], "seed", 0, 2000000000)) : 1u;
        impair.ack_loss_percent = (argc >= 9) ? parseDouble(argv[8], "ack_loss_pct", 0.0, 100.0) : 0.0;
        const std::string csvPath = (argc >= 10) ? argv[9] : "results/benchmark.csv";
        rcfg.rto_ms = 300;
        rcfg.max_retries = 20;
        rcfg.overall_timeout_ms = 30000;
        const int payload = 1200;

        if (!netInit()) {
            return 1;
        }

        std::ofstream csv(csvPath.c_str());
        csv << "strategy,resolution,jpeg_quality,jpeg_bytes,data_packets,time_s,"
               "throughput_mbps,retransmissions,mse,psnr,ssim,status\n";

        int modes[3] = {MODE_FIXED, MODE_NETWORK, MODE_IMAGE};
        int failures = 0;
        for (int i = 0; i < 3; ++i) {
            std::cout << "\n===== Benchmark strategy: " << modeName(modes[i]) << " =====\n";
            SessionResult session;
            bool ok = streamImage(imagePath, modes[i], impair, rcfg, payload, session);
            if (!ok) {
                failures++;
            }
            std::cout << "status=" << (ok ? "success" : "failure")
                      << " " << session.width << "x" << session.height
                      << " q=" << session.jpeg_quality
                      << " bytes=" << session.jpeg_bytes
                      << " retr=" << session.retransmissions
                      << " t=" << session.seconds << "s";
            if (ok) {
                std::cout << " MSE=" << session.mse
                          << " PSNR=" << session.psnr
                          << " SSIM=" << session.ssim;
            } else {
                std::cout << " reason=" << session.fail_reason;
            }
            std::cout << std::endl;
            writeRow(csv, modes[i], session);
        }

        csv.close();
        std::cout << "\nWrote " << csvPath << std::endl;
        netCleanup();
        return (failures == 0) ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        printUsage(argv[0]);
        netCleanup();
        return 1;
    }
}
