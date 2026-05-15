#include "slicing.h"
#include "logging.hpp"
#include <stdexcept>

static std::vector<std::array<int, 4>> get_slice_bboxes(
    int image_height, int image_width,
    int slice_height, int slice_width,
    float overlap_height_ratio, float overlap_width_ratio)
{
    int y_overlap = static_cast<int>(overlap_height_ratio * slice_height);
    int x_overlap = static_cast<int>(overlap_width_ratio * slice_width);

    int x_step = slice_width  - x_overlap;
    int y_step = slice_height - y_overlap;

    int n_cols = (image_width  - x_overlap + x_step - 1) / x_step;
    int n_rows = (image_height - y_overlap + y_step - 1) / y_step;

    std::vector<std::array<int, 4>> bboxes(n_rows * n_cols);

    for (int row = 0; row < n_rows; ++row) {
        for (int col = 0; col < n_cols; ++col) {
            int x_min = col * x_step;
            int y_min = row * y_step;
            int x_max = std::min(x_min + slice_width,  image_width);
            int y_max = std::min(y_min + slice_height, image_height);

            x_min = std::max(0, x_max - slice_width);
            y_min = std::max(0, y_max - slice_height);

            bboxes[row * n_cols + col] = {x_min, y_min, x_max, y_max};
        }
    }
    return bboxes;
}

static cv::Mat crop_image(const cv::Mat& image, const std::array<int, 4>& bbox) {
    cv::Rect roi(bbox[0], bbox[1], bbox[2] - bbox[0], bbox[3] - bbox[1]);
    return image(roi);  // ROI view — no copy, caller must not modify original image
}

SliceImageResult slice_image_by_size(
    const cv::Mat& image,
    int slice_height, int slice_width,
    float overlap_height_ratio,
    float overlap_width_ratio)
{
    if (overlap_height_ratio < 0.0f || overlap_height_ratio >= 1.0f)
        throw std::invalid_argument("overlap_height_ratio must be in [0, 1)");
    if (overlap_width_ratio < 0.0f || overlap_width_ratio >= 1.0f)
        throw std::invalid_argument("overlap_width_ratio must be in [0, 1)");
    if (slice_height > image.rows)
        throw std::invalid_argument("slice_height exceeds image height");
    if (slice_width > image.cols)
        throw std::invalid_argument("slice_width exceeds image width");

    LOG_DEBUG("[slice] input=[H:%d W:%d]  slice=[H:%d W:%d]  overlap=[h:%.2f w:%.2f]",
              image.rows, image.cols, slice_height, slice_width,
              overlap_height_ratio, overlap_width_ratio);

    auto bboxes = get_slice_bboxes(image.rows, image.cols,
                                   slice_height, slice_width,
                                   overlap_height_ratio, overlap_width_ratio);

    SliceImageResult result;
    result.image_height         = image.rows;
    result.image_width          = image.cols;
    result.slice_height         = slice_height;
    result.slice_width          = slice_width;
    result.overlap_height_ratio = overlap_height_ratio;
    result.overlap_width_ratio  = overlap_width_ratio;
    result.sliced_image_list.resize(bboxes.size());

    for (size_t i = 0; i < bboxes.size(); ++i) {
        result.sliced_image_list[i].image          = crop_image(image, bboxes[i]);
        result.sliced_image_list[i].starting_pixel = {bboxes[i][0], bboxes[i][1]};
    }

    LOG_DEBUG("[slice] output=%zu slices", bboxes.size());
    return result;
}
