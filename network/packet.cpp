#include "packet.h"

namespace {

void writeU16BE(unsigned char* p, uint16_t value) {
    p[0] = static_cast<unsigned char>((value >> 8) & 0xFF);
    p[1] = static_cast<unsigned char>(value & 0xFF);
}

void writeU32BE(unsigned char* p, uint32_t value) {
    p[0] = static_cast<unsigned char>((value >> 24) & 0xFF);
    p[1] = static_cast<unsigned char>((value >> 16) & 0xFF);
    p[2] = static_cast<unsigned char>((value >> 8) & 0xFF);
    p[3] = static_cast<unsigned char>(value & 0xFF);
}

uint16_t readU16BE(const unsigned char* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t readU32BE(const unsigned char* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

} // namespace

uint32_t crc32(const unsigned char* data, int length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<unsigned char> encodePacket(uint8_t type,
                                        uint32_t transfer_id,
                                        uint32_t seq,
                                        uint32_t total_packets,
                                        const unsigned char* payload,
                                        int payload_len) {
    if (payload_len < 0) {
        payload_len = 0;
    }

    std::vector<unsigned char> datagram(PKT_HEADER_SIZE + payload_len);
    writeU32BE(&datagram[0], PKT_MAGIC);
    datagram[4] = type;
    datagram[5] = 0;
    writeU16BE(&datagram[6], static_cast<uint16_t>(payload_len));
    writeU32BE(&datagram[8], transfer_id);
    writeU32BE(&datagram[12], seq);
    writeU32BE(&datagram[16], total_packets);

    uint32_t sum = 0;
    if (payload_len > 0 && payload != 0) {
        for (int i = 0; i < payload_len; ++i) {
            datagram[PKT_HEADER_SIZE + i] = payload[i];
        }
        sum = crc32(payload, payload_len);
    }
    writeU32BE(&datagram[20], sum);
    return datagram;
}

bool decodePacket(const unsigned char* data, int length, Packet& out) {
    if (data == 0 || length < PKT_HEADER_SIZE) {
        return false;
    }

    uint32_t magic = readU32BE(data);
    if (magic != PKT_MAGIC) {
        return false;
    }

    uint8_t type = data[4];
    uint8_t reserved = data[5];
    if (reserved != 0) {
        return false;
    }
    if (type != PKT_START && type != PKT_DATA && type != PKT_END) {
        return false;
    }

    uint16_t payload_len = readU16BE(data + 6);
    if (length != PKT_HEADER_SIZE + static_cast<int>(payload_len)) {
        return false;
    }
    if (payload_len > PKT_MAX_PAYLOAD) {
        return false;
    }

    uint32_t transfer_id = readU32BE(data + 8);
    uint32_t seq = readU32BE(data + 12);
    uint32_t total_packets = readU32BE(data + 16);
    uint32_t checksum = readU32BE(data + 20);

    const unsigned char* payload = data + PKT_HEADER_SIZE;
    uint32_t expected = 0;
    if (payload_len > 0) {
        expected = crc32(payload, payload_len);
    }
    if (checksum != expected) {
        return false;
    }

    out.type = type;
    out.transfer_id = transfer_id;
    out.seq = seq;
    out.total_packets = total_packets;
    out.payload_len = payload_len;
    out.checksum = checksum;
    out.payload.assign(payload, payload + payload_len);
    return true;
}

std::vector<unsigned char> encodeStartPayload(const StartInfo& info) {
    std::vector<unsigned char> p(PKT_START_PAYLOAD_SIZE);
    writeU32BE(&p[0], info.width);
    writeU32BE(&p[4], info.height);
    writeU32BE(&p[8], info.jpeg_quality);
    writeU32BE(&p[12], info.jpeg_size);
    return p;
}

bool decodeStartPayload(const std::vector<unsigned char>& payload, StartInfo& info) {
    if (payload.size() != static_cast<size_t>(PKT_START_PAYLOAD_SIZE)) {
        return false;
    }
    info.width = readU32BE(&payload[0]);
    info.height = readU32BE(&payload[4]);
    info.jpeg_quality = readU32BE(&payload[8]);
    info.jpeg_size = readU32BE(&payload[12]);
    return true;
}

void splitPayloads(const std::vector<unsigned char>& jpeg,
                   int payload_size,
                   std::vector<std::vector<unsigned char> >& parts) {
    parts.clear();
    if (payload_size <= 0) {
        payload_size = 1200;
    }
    if (jpeg.empty()) {
        return;
    }

    int offset = 0;
    while (offset < static_cast<int>(jpeg.size())) {
        int n = payload_size;
        if (offset + n > static_cast<int>(jpeg.size())) {
            n = static_cast<int>(jpeg.size()) - offset;
        }
        parts.push_back(std::vector<unsigned char>(jpeg.begin() + offset,
                                                   jpeg.begin() + offset + n));
        offset += n;
    }
}
