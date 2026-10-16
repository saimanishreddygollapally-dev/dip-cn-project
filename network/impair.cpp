#include "impair.h"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

ImpairConfig g_config;
std::vector<std::vector<unsigned char> > g_hold;
bool g_inited = false;

int extraDelayMs() {
    int extra = 0;
    if (g_config.jitter_ms > 0) {
        extra = rand() % (g_config.jitter_ms + 1);
    }
    return g_config.delay_ms + extra;
}

void sleepMs(int ms) {
    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
}

void paceForSize(int bytes) {
    if (g_config.bandwidth_kbps <= 0.0 || bytes <= 0) {
        return;
    }
    double seconds = (static_cast<double>(bytes) * 8.0) / (g_config.bandwidth_kbps * 1000.0);
    int ms = static_cast<int>(seconds * 1000.0 + 0.5);
    if (ms < 1) {
        ms = 1;
    }
    sleepMs(ms);
}

bool sendNow(SocketHandle sock,
             const std::vector<unsigned char>& datagram,
             const std::string& host,
             uint16_t port,
             ImpairStats& stats) {
    sleepMs(extraDelayMs());
    paceForSize(static_cast<int>(datagram.size()));
    if (!sendUdp(sock, datagram.data(), static_cast<int>(datagram.size()), host, port)) {
        return false;
    }
    stats.sent++;
    return true;
}

bool flushOneRandom(SocketHandle sock,
                    const std::string& host,
                    uint16_t port,
                    ImpairStats& stats) {
    if (g_hold.empty()) {
        return true;
    }
    int index = rand() % static_cast<int>(g_hold.size());
    std::vector<unsigned char> pkt = g_hold[index];
    g_hold.erase(g_hold.begin() + index);
    return sendNow(sock, pkt, host, port, stats);
}

} // namespace

void impairInit(const ImpairConfig& config) {
    g_config = config;
    if (g_config.ack_loss_percent < 0.0) {
        g_config.ack_loss_percent = 0.0;
    }
    g_hold.clear();
    g_inited = true;
    srand(g_config.seed);
}

bool impairSend(SocketHandle sock,
                const std::vector<unsigned char>& datagram,
                const std::string& host,
                uint16_t port,
                ImpairStats& stats) {
    return sendNow(sock, datagram, host, port, stats);
}

bool impairSendData(SocketHandle sock,
                    const std::vector<unsigned char>& datagram,
                    const std::string& host,
                    uint16_t port,
                    ImpairStats& stats) {
    stats.generated++;

    if (g_config.loss_percent > 0.0) {
        int r = rand() % 10000;
        int threshold = static_cast<int>(g_config.loss_percent * 100.0);
        if (r < threshold) {
            stats.dropped++;
            return true;
        }
    }

    if (g_config.reorder_window >= 2) {
        g_hold.push_back(datagram);
        if (static_cast<int>(g_hold.size()) >= g_config.reorder_window) {
            return flushOneRandom(sock, host, port, stats);
        }
        return true;
    }

    return sendNow(sock, datagram, host, port, stats);
}

bool impairFlush(SocketHandle sock,
                 const std::string& host,
                 uint16_t port,
                 ImpairStats& stats) {
    while (!g_hold.empty()) {
        if (!flushOneRandom(sock, host, port, stats)) {
            return false;
        }
    }
    return true;
}

bool impairRetransmitData(SocketHandle sock,
                          const std::vector<unsigned char>& datagram,
                          const std::string& host,
                          uint16_t port,
                          ImpairStats& stats) {
    int before = stats.generated;
    bool ok = impairSendData(sock, datagram, host, port, stats);
    stats.generated = before;
    return ok;
}

bool impairSendAck(SocketHandle sock,
                   const std::vector<unsigned char>& datagram,
                   const std::string& host,
                   uint16_t port,
                   ImpairStats& stats) {
    if (g_config.ack_loss_percent > 0.0) {
        int r = rand() % 10000;
        int threshold = static_cast<int>(g_config.ack_loss_percent * 100.0);
        if (r < threshold) {
            stats.acks_dropped++;
            return true;
        }
    }
    return sendNow(sock, datagram, host, port, stats);
}
