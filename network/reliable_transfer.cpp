#include "reliable_transfer.h"

#include "packet.h"
#include "udp_socket.h"

#include <chrono>
#include <iostream>
#include <vector>

namespace {

bool allAcked(const std::vector<char>& acked) {
    for (size_t i = 0; i < acked.size(); ++i) {
        if (!acked[i]) {
            return false;
        }
    }
    return true;
}

bool waitForControl(SocketHandle sock,
                    uint32_t transferId,
                    uint8_t wantType,
                    uint32_t wantSeq,
                    int rtoMs,
                    ReliableResult& result) {
    auto t0 = std::chrono::steady_clock::now();
    const int kMax = PKT_HEADER_SIZE + PKT_MAX_PAYLOAD;
    std::vector<unsigned char> buf(kMax);

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count() >= rtoMs) {
            return false;
        }
        int n = recvUdp(sock, buf.data(), kMax);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            continue;
        }
        Packet pkt;
        if (!decodePacket(buf.data(), n, pkt)) {
            continue;
        }
        if (pkt.transfer_id != transferId) {
            continue;
        }
        if (pkt.type == PKT_ACK) {
            result.acks_received++;
        } else if (pkt.type == PKT_NACK) {
            result.nacks_received++;
        }
        if (pkt.type == wantType && pkt.seq == wantSeq) {
            return true;
        }
    }
}

} // namespace

bool sendReliableJpeg(SocketHandle sock,
                      const std::vector<unsigned char>& jpeg,
                      const StartInfo& meta,
                      int payload_size,
                      const ReliableConfig& cfg,
                      ReliableResult& result) {
    result.success = false;
    result.fail_reason = "";
    result.packets_generated = 0;
    result.packets_sent = 0;
    result.packets_dropped = 0;
    result.acks_received = 0;
    result.nacks_received = 0;
    result.retransmissions = 0;
    result.retries_exhausted = 0;
    result.seconds = 0.0;

    std::vector<std::vector<unsigned char> > parts;
    splitPayloads(jpeg, payload_size, parts);
    uint32_t totalPackets = static_cast<uint32_t>(parts.size());
    if (totalPackets == 0) {
        result.fail_reason = "empty JPEG";
        return false;
    }

    result.transfer_id = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);

    std::vector<unsigned char> startPayload = encodeStartPayload(meta);
    std::vector<unsigned char> startPkt = encodePacket(
        PKT_START, result.transfer_id, 0, totalPackets,
        startPayload.data(), static_cast<int>(startPayload.size()));

    std::vector<std::vector<unsigned char> > dataPkts;
    dataPkts.reserve(totalPackets);
    for (uint32_t i = 0; i < totalPackets; ++i) {
        dataPkts.push_back(encodePacket(
            PKT_DATA, result.transfer_id, i, totalPackets,
            parts[i].data(), static_cast<int>(parts[i].size())));
    }

    std::vector<unsigned char> endPkt = encodePacket(
        PKT_END, result.transfer_id, totalPackets, totalPackets, 0, 0);

    ImpairStats stats;
    stats.generated = 0;
    stats.sent = 0;
    stats.dropped = 0;
    stats.acks_dropped = 0;

    if (!setUdpRecvTimeout(sock, cfg.rto_ms)) {
        result.fail_reason = "recv timeout setup failed";
        return false;
    }

    auto t0 = std::chrono::steady_clock::now();

    bool startOk = false;
    for (int attempt = 0; attempt <= cfg.max_retries; ++attempt) {
        if (attempt > 0) {
            result.retransmissions++;
        }
        if (!impairSend(sock, startPkt, cfg.host, cfg.port, stats)) {
            result.fail_reason = "START send failed";
            return false;
        }
        if (waitForControl(sock, result.transfer_id, PKT_ACK, SEQ_ACK_START,
                           cfg.rto_ms, result)) {
            startOk = true;
            break;
        }
    }
    if (!startOk) {
        result.fail_reason = "START not acknowledged";
        result.retries_exhausted = 1;
        result.packets_generated = stats.generated;
        result.packets_sent = stats.sent;
        result.packets_dropped = stats.dropped;
        return false;
    }

    for (uint32_t i = 0; i < totalPackets; ++i) {
        if (!impairSendData(sock, dataPkts[i], cfg.host, cfg.port, stats)) {
            result.fail_reason = "DATA send failed";
            return false;
        }
    }
    if (!impairFlush(sock, cfg.host, cfg.port, stats)) {
        result.fail_reason = "reorder flush failed";
        return false;
    }

    std::vector<char> acked(totalPackets, 0);
    std::vector<int> retries(totalPackets, 0);
    const int kMax = PKT_HEADER_SIZE + PKT_MAX_PAYLOAD;
    std::vector<unsigned char> buf(kMax);

    while (!allAcked(acked)) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (elapsed * 1000.0 > cfg.overall_timeout_ms) {
            result.fail_reason = "overall timeout before all DATA ACKs";
            result.packets_generated = stats.generated;
            result.packets_sent = stats.sent;
            result.packets_dropped = stats.dropped;
            result.seconds = elapsed;
            return false;
        }

        int n = recvUdp(sock, buf.data(), kMax);
        if (n < 0) {
            result.fail_reason = "recv error";
            return false;
        }

        if (n > 0) {
            Packet pkt;
            if (!decodePacket(buf.data(), n, pkt) || pkt.transfer_id != result.transfer_id) {
                continue;
            }
            if (pkt.type == PKT_ACK && pkt.seq < totalPackets) {
                result.acks_received++;
                acked[pkt.seq] = 1;
                continue;
            }
            if (pkt.type == PKT_NACK && pkt.seq < totalPackets && !acked[pkt.seq]) {
                result.nacks_received++;
                if (retries[pkt.seq] >= cfg.max_retries) {
                    result.fail_reason = "retry limit exhausted";
                    result.retries_exhausted = 1;
                    result.packets_generated = stats.generated;
                    result.packets_sent = stats.sent;
                    result.packets_dropped = stats.dropped;
                    result.seconds = elapsed;
                    return false;
                }
                retries[pkt.seq]++;
                result.retransmissions++;
                if (!impairRetransmitData(sock, dataPkts[pkt.seq], cfg.host, cfg.port, stats)) {
                    result.fail_reason = "retransmit failed";
                    return false;
                }
                impairFlush(sock, cfg.host, cfg.port, stats);
            }
            continue;
        }

        // RTO: selectively retransmit every unacked DATA packet.
        for (uint32_t i = 0; i < totalPackets; ++i) {
            if (acked[i]) {
                continue;
            }
            if (retries[i] >= cfg.max_retries) {
                result.fail_reason = "retry limit exhausted";
                result.retries_exhausted = 1;
                result.packets_generated = stats.generated;
                result.packets_sent = stats.sent;
                result.packets_dropped = stats.dropped;
                result.seconds = elapsed;
                return false;
            }
            retries[i]++;
            result.retransmissions++;
            if (!impairRetransmitData(sock, dataPkts[i], cfg.host, cfg.port, stats)) {
                result.fail_reason = "retransmit failed";
                return false;
            }
        }
        impairFlush(sock, cfg.host, cfg.port, stats);
    }

    bool endOk = false;
    for (int attempt = 0; attempt <= cfg.max_retries; ++attempt) {
        if (attempt > 0) {
            result.retransmissions++;
        }
        if (!impairSend(sock, endPkt, cfg.host, cfg.port, stats)) {
            result.fail_reason = "END send failed";
            return false;
        }
        if (waitForControl(sock, result.transfer_id, PKT_ACK, SEQ_ACK_END,
                           cfg.rto_ms, result)) {
            endOk = true;
            break;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(t1 - t0).count();
    result.packets_generated = stats.generated;
    result.packets_sent = stats.sent;
    result.packets_dropped = stats.dropped;

    if (!endOk) {
        result.fail_reason = "END not acknowledged";
        result.retries_exhausted = 1;
        return false;
    }

    result.success = true;
    return true;
}
