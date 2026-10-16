#ifndef STREAM_SESSION_H
#define STREAM_SESSION_H

#include "adaptive.h"
#include "impair.h"
#include "reliable_transfer.h"

#include <string>

struct SessionResult {
    bool success;
    std::string fail_reason;
    int width;
    int height;
    int jpeg_quality;
    int jpeg_bytes;
    int data_packets;
    double seconds;
    double throughput_mbps;
    double mse;
    double psnr;
    double ssim;
    int retransmissions;
    ReliableResult net;
    EncodeChoice choice;
};

bool streamImage(const std::string& imagePath,
                 int mode,
                 const ImpairConfig& impair,
                 const ReliableConfig& reliable,
                 int payload_size,
                 SessionResult& out);

#endif // STREAM_SESSION_H
