#ifndef RELIABLE_TRANSFER_H
#define RELIABLE_TRANSFER_H

#include "impair.h"
#include "packet.h"

#include <string>
#include <vector>

struct ReliableConfig {
    std::string host;
    uint16_t port;
    int rto_ms;
    int max_retries;
    int overall_timeout_ms;
};

struct ReliableResult {
    bool success;
    std::string fail_reason;
    uint32_t transfer_id;
    int packets_generated;
    int packets_sent;
    int packets_dropped;
    int acks_received;
    int nacks_received;
    int retransmissions;
    int retries_exhausted;
    double seconds;
};

// Selective-repeat style: ACK per DATA seq, NACK for gaps, bounded retries.
bool sendReliableJpeg(SocketHandle sock,
                      const std::vector<unsigned char>& jpeg,
                      const StartInfo& meta,
                      int payload_size,
                      const ReliableConfig& cfg,
                      ReliableResult& result);

#endif // RELIABLE_TRANSFER_H
