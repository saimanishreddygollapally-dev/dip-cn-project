#include "image_processing.h"

#include <iostream>

int main(int argc, char* argv[]) {
    cv::Mat empty;
    double badEmpty = computeSSIM(empty, empty);
    std::cout << "SSIM empty images: " << badEmpty << std::endl;

    cv::Mat a(32, 32, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::Mat b(16, 16, CV_8UC3, cv::Scalar(10, 20, 30));
    double badSize = computeSSIM(a, b);
    std::cout << "SSIM mismatched size: " << badSize << std::endl;

    double same = computeSSIM(a, a);
    std::cout << "SSIM identical 32x32: " << same << std::endl;

    if (argc >= 3) {
        cv::Mat x = loadImage(argv[1]);
        cv::Mat y = loadImage(argv[2]);
        if (x.size() != y.size()) {
            y = resizeImage(y, x.cols, x.rows);
        }
        double s = computeSSIM(x, y);
        std::cout << "SSIM files: " << s << std::endl;
    }

    if (badEmpty < 0.0 && badSize < 0.0 && same > 0.99) {
        std::cout << "ssim_check passed." << std::endl;
        return 0;
    }
    std::cout << "ssim_check failed." << std::endl;
    return 1;
}
