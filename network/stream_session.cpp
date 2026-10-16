#include "stream_session.h"

#include "image_processing.h"
#include "tcp_socket.h"
#include "udp_socket.h"

#include <iostream>

bool streamImage(const std::string& imagePath,
                 int mode,
                 const ImpairConfig& impair,
                 const ReliableConfig& reliable,
                 int payload_size,
                 SessionResult& out) {
    out.success = false;
    out.mse = -1.0;
    out.psnr = -1.0;
    out.ssim = -1.0;
    out.throughput_mbps = 0.0;
    out.retransmissions = 0;
    out.jpeg_bytes = 0;
    out.data_packets = 0;

    cv::Mat original = loadImage(imagePath);
    cv::Mat gray = toGrayscale(original);
    int histogram[256];
    computeHistogram(gray, histogram);
    double entropy = computeEntropy(histogram);
    cv::Mat blurred = applyGaussianFilter(gray, 5, 1.0);
    cv::Mat edges = detectEdgesSobel(blurred);
    double edgeDensity = computeEdgeDensity(edges, 50);
    printDipSummary(gray, histogram, entropy, edgeDensity);

    ImageComplexity cx;
    cx.orig_width = original.cols;
    cx.orig_height = original.rows;
    cx.entropy = entropy;
    cx.edge_density = edgeDensity;

    NetworkEstimate net;
    net.bandwidth_kbps = impair.bandwidth_kbps;
    net.loss_percent = impair.loss_percent;
    net.prior_retransmissions = 0;

    out.choice = chooseEncoding(mode, net, cx);
    out.width = out.choice.width;
    out.height = out.choice.height;
    out.jpeg_quality = out.choice.jpeg_quality;

    std::cout << "Adaptive mode: " << mode
              << " network=" << out.choice.network_class
              << " complexity=" << out.choice.complexity_class
              << " -> " << out.width << "x" << out.height
              << " q=" << out.jpeg_quality << std::endl;

    cv::Mat resized = resizeImage(original, out.width, out.height);
    std::vector<uchar> jpeg;
    if (!compressToJpeg(resized, jpeg, out.jpeg_quality)) {
        out.fail_reason = "JPEG compression failed";
        return false;
    }
    out.jpeg_bytes = static_cast<int>(jpeg.size());
    out.data_packets = (out.jpeg_bytes + payload_size - 1) / payload_size;

    StartInfo meta;
    meta.width = static_cast<uint32_t>(out.width);
    meta.height = static_cast<uint32_t>(out.height);
    meta.jpeg_quality = static_cast<uint32_t>(out.jpeg_quality);
    meta.jpeg_size = static_cast<uint32_t>(jpeg.size());

    SocketHandle sock = createUdpSocket();
    if (sock == INVALID_SOCKET) {
        out.fail_reason = "UDP socket failed";
        return false;
    }

    ImpairConfig local = impair;
    impairInit(local);

    bool ok = sendReliableJpeg(sock, jpeg, meta, payload_size, reliable, out.net);
    closeSocket(sock);

    out.seconds = out.net.seconds;
    out.retransmissions = out.net.retransmissions;
    out.success = ok && out.net.success;
    out.fail_reason = out.net.fail_reason;

    if (out.seconds > 0.0 && out.jpeg_bytes > 0) {
        out.throughput_mbps = (static_cast<double>(out.jpeg_bytes) * 8.0) /
                              (out.seconds * 1e6);
    }

    if (!out.success) {
        return false;
    }

    cv::Mat decoded = decompressJpeg(jpeg);
    out.mse = computeMSE(resized, decoded);
    out.psnr = computePSNR(out.mse);
    out.ssim = computeSSIM(resized, decoded);
    return true;
}
