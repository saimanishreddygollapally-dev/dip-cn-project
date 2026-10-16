#include "adaptive.h"

#include <algorithm>

namespace {

int evenAtLeast(int value, int minValue) {
    if (value < minValue) {
        value = minValue;
    }
    if (value % 2 != 0) {
        value -= 1;
    }
    if (value < minValue) {
        value = minValue;
    }
    return value;
}

void fitInside(int origW, int origH, int boxW, int boxH, int& outW, int& outH) {
    if (origW <= 0) {
        origW = boxW;
    }
    if (origH <= 0) {
        origH = boxH;
    }
    double scaleW = static_cast<double>(boxW) / static_cast<double>(origW);
    double scaleH = static_cast<double>(boxH) / static_cast<double>(origH);
    double scale = (scaleW < scaleH) ? scaleW : scaleH;
    if (scale > 1.0) {
        scale = 1.0;
    }
    outW = evenAtLeast(static_cast<int>(origW * scale + 0.5), 16);
    outH = evenAtLeast(static_cast<int>(origH * scale + 0.5), 16);
}

// 0=good, 1=medium, 2=poor. Take the worst of the available signals.
int networkClass(const NetworkEstimate& net) {
    int worst = 0;

    if (net.bandwidth_kbps > 0.0) {
        // Thresholds: below 250 kbps cannot carry a large JPEG quickly.
        if (net.bandwidth_kbps < 250.0) {
            worst = 2;
        } else if (net.bandwidth_kbps < 800.0) {
            worst = 1;
        }
    }

    // 8% loss already causes several DATA drops on a ~20-packet image.
    if (net.loss_percent >= 20.0) {
        worst = std::max(worst, 2);
    } else if (net.loss_percent >= 8.0) {
        worst = std::max(worst, 1);
    }

    if (net.prior_retransmissions >= 8) {
        worst = std::max(worst, 2);
    } else if (net.prior_retransmissions >= 3) {
        worst = std::max(worst, 1);
    }

    return worst;
}

// High entropy (~7 bits) or many edges means JPEG artifacts are more visible,
// so the image-aware mode spends extra bits or keeps more spatial detail.
int complexityClass(const ImageComplexity& image) {
    if (image.entropy >= 6.5 || image.edge_density >= 0.06) {
        return 2;
    }
    if (image.entropy >= 5.0 || image.edge_density >= 0.025) {
        return 1;
    }
    return 0;
}

const char* netName(int c) {
    if (c >= 2) {
        return "poor";
    }
    if (c == 1) {
        return "medium";
    }
    return "good";
}

const char* cxName(int c) {
    if (c >= 2) {
        return "complex";
    }
    if (c == 1) {
        return "moderate";
    }
    return "simple";
}

} // namespace

EncodeChoice chooseEncoding(int mode,
                            const NetworkEstimate& net,
                            const ImageComplexity& image) {
    EncodeChoice choice;
    choice.network_class = netName(networkClass(net));
    choice.complexity_class = cxName(complexityClass(image));

    int boxW = 640;
    int boxH = 480;
    int quality = 75;

    if (mode == MODE_FIXED) {
        boxW = 640;
        boxH = 480;
        quality = 75;
    } else if (mode == MODE_NETWORK) {
        int n = networkClass(net);
        // Network-only: ignore entropy / edge density on purpose.
        if (n >= 2) {
            boxW = 320;
            boxH = 240;
            quality = 40;
        } else if (n == 1) {
            boxW = 480;
            boxH = 360;
            quality = 55;
        } else {
            boxW = 640;
            boxH = 480;
            quality = 80;
        }
    } else {
        int n = networkClass(net);
        int c = complexityClass(image);
        if (n >= 2) {
            boxW = 320;
            boxH = 240;
            // Poor network: drop resolution. Complex images keep slightly
            // higher JPEG quality so edges survive the extra compression.
            quality = (c >= 2) ? 50 : ((c == 1) ? 45 : 40);
        } else if (n == 1) {
            boxW = 480;
            boxH = 360;
            quality = (c >= 2) ? 70 : ((c == 1) ? 60 : 50);
        } else {
            boxW = 640;
            boxH = 480;
            // Good network: complex images get more bits; simple images
            // can use a lower JPEG quality without much visual loss.
            quality = (c >= 2) ? 90 : ((c == 1) ? 80 : 70);
        }
    }

    if (quality < 30) {
        quality = 30;
    }
    if (quality > 95) {
        quality = 95;
    }

    fitInside(image.orig_width, image.orig_height, boxW, boxH,
              choice.width, choice.height);
    choice.jpeg_quality = quality;
    return choice;
}
