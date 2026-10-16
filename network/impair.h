#ifndef IMPAIR_H
#define IMPAIR_H

#include "udp_socket.h"

#include <string>
#include <vector>

// Sender-side unreliable-network simulation (Day 2).
// This is a simple local emulator, not a general network testbed.
//
// Units:
//   loss_percent    0-100   chance a DATA packet is never sent
//   delay_ms        >= 0    base delay before each datagram
//   jitter_ms       >= 0    extra delay chosen uniformly in 0..jitter_ms
//   bandwidth_kbps  >= 0    0 means unlimited; otherwise pace sends
//   reorder_window  >= 0    0/1 means send DATA in order; >=2 holds that
//                           many DATA packets and sends one at random
//
// Loss and reordering apply to DATA packets only so START/END can still
// mark the transfer. Delay, jitter, and bandwidth apply to every datagram.

struct ImpairConfig {
    double loss_percent;
    int delay_ms;
    int jitter_ms;
    double bandwidth_kbps;
    int reorder_window;
    unsigned int seed;
};

struct ImpairStats {
    int generated;
    int sent;
    int dropped;
};

void impairInit(const ImpairConfig& config);

// Apply delay/jitter/bandwidth, then send. Returns false only if sendto fails.
bool impairSend(SocketHandle sock,
                const std::vector<unsigned char>& datagram,
                const std::string& host,
                uint16_t port,
                ImpairStats& stats);

// DATA packets may be held for reordering, dropped, then sent.
bool impairSendData(SocketHandle sock,
                    const std::vector<unsigned char>& datagram,
                    const std::string& host,
                    uint16_t port,
                    ImpairStats& stats);

// Send any DATA packets still held in the reorder buffer.
bool impairFlush(SocketHandle sock,
                 const std::string& host,
                 uint16_t port,
                 ImpairStats& stats);

#endif // IMPAIR_H
