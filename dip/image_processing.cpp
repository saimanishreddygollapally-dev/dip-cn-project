#include "image_processing.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

#include <opencv2/quality.hpp>

cv::Mat loadImage(const std::string& path) {
    cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        throw std::runtime_error("Failed to load image: " + path);
    }
    return image;
}

cv::Mat toGrayscale(const cv::Mat& image) {
    if (image.channels() == 1) {
        return image.clone();
    }
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

void computeHistogram(const cv::Mat& gray, int histogram[256]) {
    for (int i = 0; i < 256; ++i) {
        histogram[i] = 0;
    }

    cv::Mat work = gray;
    if (work.channels() != 1) {
        work = toGrayscale(gray);
    }

    for (int r = 0; r < work.rows; ++r) {
        const uchar* row = work.ptr<uchar>(r);
        for (int c = 0; c < work.cols; ++c) {
            histogram[row[c]]++;
        }
    }
}

double computeEntropy(const int histogram[256]) {
    long total = 0;
    for (int i = 0; i < 256; ++i) {
        total += histogram[i];
    }
    if (total <= 0) {
        return 0.0;
    }

    double entropy = 0.0;
    for (int i = 0; i < 256; ++i) {
        if (histogram[i] == 0) {
            continue;
        }
        double p = static_cast<double>(histogram[i]) / static_cast<double>(total);
        entropy -= p * std::log2(p);
    }
    return entropy;
}

cv::Mat applyGaussianFilter(const cv::Mat& image, int ksize, double sigma) {
    if (ksize % 2 == 0) {
        ksize += 1; // OpenCV requires odd kernel size
    }
    cv::Mat blurred;
    cv::GaussianBlur(image, blurred, cv::Size(ksize, ksize), sigma);
    return blurred;
}

cv::Mat detectEdgesSobel(const cv::Mat& gray) {
    cv::Mat work = gray;
    if (work.channels() != 1) {
        work = toGrayscale(gray);
    }

    cv::Mat gradX, gradY;
    cv::Sobel(work, gradX, CV_16S, 1, 0, 3);
    cv::Sobel(work, gradY, CV_16S, 0, 1, 3);

    cv::Mat absX, absY, edges;
    cv::convertScaleAbs(gradX, absX);
    cv::convertScaleAbs(gradY, absY);
    cv::addWeighted(absX, 0.5, absY, 0.5, 0.0, edges);
    return edges;
}

double computeEdgeDensity(const cv::Mat& edges, int threshold) {
    if (edges.empty()) {
        return 0.0;
    }

    long edgePixels = 0;
    long totalPixels = static_cast<long>(edges.rows) * static_cast<long>(edges.cols);

    for (int r = 0; r < edges.rows; ++r) {
        const uchar* row = edges.ptr<uchar>(r);
        for (int c = 0; c < edges.cols; ++c) {
            if (row[c] > threshold) {
                edgePixels++;
            }
        }
    }

    if (totalPixels == 0) {
        return 0.0;
    }
    return static_cast<double>(edgePixels) / static_cast<double>(totalPixels);
}

cv::Mat resizeImage(const cv::Mat& image, int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid resize dimensions");
    }
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(width, height), 0, 0, cv::INTER_AREA);
    return resized;
}

bool compressToJpeg(const cv::Mat& image, std::vector<uchar>& buffer, int quality) {
    if (quality < 0) {
        quality = 0;
    }
    if (quality > 100) {
        quality = 100;
    }

    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(quality);

    buffer.clear();
    return cv::imencode(".jpg", image, buffer, params);
}

cv::Mat decompressJpeg(const std::vector<uchar>& buffer) {
    if (buffer.empty()) {
        throw std::runtime_error("Empty JPEG buffer");
    }
    cv::Mat image = cv::imdecode(buffer, cv::IMREAD_COLOR);
    if (image.empty()) {
        throw std::runtime_error("Failed to decode JPEG buffer");
    }
    return image;
}

double computeMSE(const cv::Mat& original, const cv::Mat& reconstructed) {
    if (original.empty() || reconstructed.empty()) {
        throw std::runtime_error("Cannot compute MSE on empty images");
    }
    if (original.size() != reconstructed.size() || original.type() != reconstructed.type()) {
        throw std::runtime_error("MSE requires images with the same size and type");
    }

    cv::Mat diff;
    cv::absdiff(original, reconstructed, diff);
    diff.convertTo(diff, CV_32F);
    diff = diff.mul(diff);

    cv::Scalar sumSq = cv::sum(diff);
    double sse = 0.0;
    for (int i = 0; i < original.channels(); ++i) {
        sse += sumSq[i];
    }

    double denom = static_cast<double>(original.rows) *
                   static_cast<double>(original.cols) *
                   static_cast<double>(original.channels());
    return sse / denom;
}

double computePSNR(double mse, double maxPixelValue) {
    if (mse <= 1e-10) {
        return 99.0; // essentially identical images
    }
    return 10.0 * std::log10((maxPixelValue * maxPixelValue) / mse);
}

double computeSSIM(const cv::Mat& original, const cv::Mat& reconstructed) {
    if (original.empty() || reconstructed.empty()) {
        return -1.0;
    }
    if (original.size() != reconstructed.size()) {
        return -1.0;
    }

    cv::Mat a = original;
    cv::Mat b = reconstructed;
    if (a.channels() != b.channels()) {
        return -1.0;
    }
    if (a.channels() != 1) {
        a = toGrayscale(a);
        b = toGrayscale(b);
    }

    cv::Scalar s = cv::quality::QualitySSIM::compute(a, b, cv::noArray());
    return s[0];
}

void printDipSummary(const cv::Mat& gray,
                     const int histogram[256],
                     double entropy,
                     double edgeDensity) {
    int minBin = 0;
    int maxBin = 0;
    for (int i = 0; i < 256; ++i) {
        if (histogram[i] > 0) {
            minBin = i;
            break;
        }
    }
    for (int i = 255; i >= 0; --i) {
        if (histogram[i] > 0) {
            maxBin = i;
            break;
        }
    }

    std::cout << "=== DIP Summary ===" << std::endl;
    std::cout << "Size: " << gray.cols << " x " << gray.rows << std::endl;
    std::cout << "Histogram range: [" << minBin << ", " << maxBin << "]" << std::endl;
    std::cout << "Entropy: " << entropy << " bits" << std::endl;
    std::cout << "Edge density: " << edgeDensity << std::endl;
}
