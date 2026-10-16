#ifndef ADAPTIVE_H
#define ADAPTIVE_H

// Simple rule-based encoder. No ML.
// MODE_FIXED: always 640x480 JPEG 75 (Day 1 baseline).
// MODE_NETWORK: uses configured bandwidth, loss, and prior retransmits only.
// MODE_IMAGE: also uses entropy and edge density.

const int MODE_FIXED = 0;
const int MODE_NETWORK = 1;
const int MODE_IMAGE = 2;

struct NetworkEstimate {
    double bandwidth_kbps;     // 0 = unlimited / unknown-good
    double loss_percent;       // configured or observed 0-100
    int prior_retransmissions; // 0 on the first transfer
};

struct ImageComplexity {
    int orig_width;
    int orig_height;
    double entropy;
    double edge_density;
};

struct EncodeChoice {
    int width;
    int height;
    int jpeg_quality;
    const char* network_class;
    const char* complexity_class;
};

EncodeChoice chooseEncoding(int mode,
                            const NetworkEstimate& net,
                            const ImageComplexity& image);

#endif // ADAPTIVE_H
