#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>

// Creates a simple synthetic test image for Day-1 localhost testing.
int main(int argc, char* argv[]) {
    const std::string outPath = (argc >= 2) ? argv[1] : "images/sample.jpg";

    cv::Mat image(480, 640, CV_8UC3);
    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            image.at<cv::Vec3b>(y, x) = cv::Vec3b(
                static_cast<uchar>(x % 256),
                static_cast<uchar>(y % 256),
                static_cast<uchar>((x + y) % 256));
        }
    }

    // Add a few shapes so edge density / entropy are non-trivial.
    cv::rectangle(image, cv::Rect(80, 60, 200, 140), cv::Scalar(20, 180, 255), -1);
    cv::circle(image, cv::Point(420, 240), 90, cv::Scalar(255, 80, 40), -1);
    cv::line(image, cv::Point(40, 400), cv::Point(600, 420), cv::Scalar(255, 255, 255), 4);
    cv::putText(image, "Day1 Sample", cv::Point(180, 40),
                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 0), 2);

    if (!cv::imwrite(outPath, image)) {
        std::cerr << "Failed to write " << outPath << std::endl;
        return 1;
    }

    std::cout << "Wrote sample image: " << outPath << std::endl;
    return 0;
}
