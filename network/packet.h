#ifndef PACKET_H
#define PACKET_H

#include <cstdint>
#include <vector>

// Day 2 UDP packet format (not a raw C++ struct on the wire).
//
// All multi-byte fields are big-endian (network byte order).
//
// Header (24 bytes):
//   0-3   magic          uint32  'AIST' = 0x41495354
//   4     type           uint8   1=START, 2=DATA, 3=END
//   5     reserved       uint8   must be 0
//   6-7   payload_len    uint16  number of payload bytes after the header
//   8-11  transfer_id    uint32
//  12-15  seq            uint32  DATA: 0 .. total_packets-1
//  16-19  total_packets  uint32  number of DATA packets in this transfer
//  20-23  checksum       uint32  CRC-32 of the payload only (0 if payload_len is 0)
//  24...  payload        payload_len bytes
//
// START payload (16 bytes):
//   width, height, jpeg_quality, jpeg_size  (each uint32 big-endian)
//
// DATA payload: one JPEG slice.
// END payload: empty.
//
// CRC-32 uses the Ethernet/ISO 3309 polynomial. It detects accidental
// corruption; it is not cryptographic security.

const uint32_t PKT_MAGIC = 0x41495354u;
const uint8_t PKT_START = 1;
const uint8_t PKT_DATA = 2;
const uint8_t PKT_END = 3;
const int PKT_HEADER_SIZE = 24;
const int PKT_START_PAYLOAD_SIZE = 16;
const int PKT_MAX_PAYLOAD = 1400;

struct Packet {
    uint8_t type;
    uint32_t transfer_id;
    uint32_t seq;
    uint32_t total_packets;
    uint16_t payload_len;
    uint32_t checksum;
    std::vector<unsigned char> payload;
};

struct StartInfo {
    uint32_t width;
    uint32_t height;
    uint32_t jpeg_quality;
    uint32_t jpeg_size;
};

uint32_t crc32(const unsigned char* data, int length);

std::vector<unsigned char> encodePacket(uint8_t type,
                                        uint32_t transfer_id,
                                        uint32_t seq,
                                        uint32_t total_packets,
                                        const unsigned char* payload,
                                        int payload_len);

bool decodePacket(const unsigned char* data, int length, Packet& out);

std::vector<unsigned char> encodeStartPayload(const StartInfo& info);
bool decodeStartPayload(const std::vector<unsigned char>& payload, StartInfo& info);

// Split JPEG bytes into DATA payloads of at most payload_size bytes.
void splitPayloads(const std::vector<unsigned char>& jpeg,
                   int payload_size,
                   std::vector<std::vector<unsigned char> >& parts);

#endif // PACKET_H
