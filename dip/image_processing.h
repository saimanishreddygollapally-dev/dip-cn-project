#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// Load an image from disk (BGR color).
cv::Mat loadImage(const std::string& path);

// Convert BGR/color image to grayscale.
cv::Mat toGrayscale(const cv::Mat& image);

// Compute 256-bin intensity histogram of a grayscale image.
void computeHistogram(const cv::Mat& gray, int histogram[256]);

// Shannon entropy from a 256-bin histogram (bits).
double computeEntropy(const int histogram[256]);

// Gaussian blur (noise reduction / preprocessing).
cv::Mat applyGaussianFilter(const cv::Mat& image, int ksize = 5, double sigma = 1.0);

// Sobel edge magnitude image (CV_8U).
cv::Mat detectEdgesSobel(const cv::Mat& gray);

// Fraction of pixels above threshold in an edge image (0.0 - 1.0).
double computeEdgeDensity(const cv::Mat& edges, int threshold = 50);

// Resize image to given width and height.
cv::Mat resizeImage(const cv::Mat& image, int width, int height);

// JPEG-compress image into a byte buffer. quality is 0-100.
bool compressToJpeg(const cv::Mat& image, std::vector<uchar>& buffer, int quality);

// Decode JPEG bytes back into a BGR image.
cv::Mat decompressJpeg(const std::vector<uchar>& buffer);

// Mean Squared Error between two images of the same size.
double computeMSE(const cv::Mat& original, const cv::Mat& reconstructed);

// Peak Signal-to-Noise Ratio in dB (from MSE).
double computePSNR(double mse, double maxPixelValue = 255.0);

// Structural Similarity (0-1 typical). Returns -1 if images cannot be compared.
// This is a quality metric, not an exact perceptual judgment.
double computeSSIM(const cv::Mat& original, const cv::Mat& reconstructed);

// Convenience: print a short DIP summary for a grayscale analysis image.
void printDipSummary(const cv::Mat& gray,
                     const int histogram[256],
                     double entropy,
                     double edgeDensity);

#endif // IMAGE_PROCESSING_H
