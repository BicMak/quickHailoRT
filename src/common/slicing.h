#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <array>

struct SlicedImage {
    cv::Mat image;
    std::array<int, 2> starting_pixel;  // [x, y] 좌상단
};

struct SliceImageResult {
    int image_height;
    int image_width;
    int slice_height;
    int slice_width;
    float overlap_height_ratio;
    float overlap_width_ratio;
    std::vector<SlicedImage> sliced_image_list;
};

SliceImageResult slice_image_by_size(
    const cv::Mat& image,
    int slice_height, int slice_width,
    float overlap_height_ratio = 0.2f,
    float overlap_width_ratio  = 0.2f);
