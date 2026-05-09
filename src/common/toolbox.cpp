#include "toolbox.hpp"
#include "logging.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

namespace hailo_utils {
namespace fs = std::filesystem;

std::vector<cv::Scalar> COLORS = {
    {  0, 113, 188}, {216,  82,  24}, {236, 176,  31}, {125,  46, 141},
    {118, 171,  47}, { 76, 189, 237}, {161,  19,  46}, { 76,  76,  76},
    {153, 153, 153}, {255,   0,   0}, {255, 128,   0}, {191, 191,   0},
    {  0, 255,   0}, {  0,   0, 255}, {170,   0, 255}, { 85,  85,   0},
};

const std::unordered_map<std::string, std::pair<int,int>> RESOLUTION_MAP = {
    {"sd",  {640, 480}},
    {"hd",  {1280, 720}},
    {"fhd", {1920, 1080}}
};

// ─────────────────────────────────────────────────────────────────────────────
// FILE / PATH
// ─────────────────────────────────────────────────────────────────────────────

bool is_image_file(const std::string &path) {
    static const std::vector<std::string> exts = {
        ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".webp"
    };
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    bool result = std::find(exts.begin(), exts.end(), ext) != exts.end();
    LOG_TRACE("is_image_file: %s -> %s", path.c_str(), result ? "yes" : "no");
    return result;
}

bool is_directory_of_images(const std::string &path, size_t &entry_count, size_t batch_size) {
    entry_count = 0;
    if (!fs::exists(path) || !fs::is_directory(path)) {
        LOG_WARN("is_directory_of_images: path not found or not a directory: %s", path.c_str());
        return false;
    }
    bool has_images = false;
    for (const auto &entry : fs::directory_iterator(path)) {
        if (!fs::is_regular_file(entry)) continue;
        entry_count++;
        if (!is_image_file(entry.path().string())) return false;
        has_images = true;
    }
    if (entry_count % batch_size != 0) {
        throw std::invalid_argument("Directory contains " + std::to_string(entry_count) +
            " images, not divisible by batch size " + std::to_string(batch_size));
    }
    LOG_TRACE("is_directory_of_images: %s -> %zu images", path.c_str(), entry_count);
    return has_images;
}

bool is_image(const std::string &path) {
    bool result = fs::exists(path) && fs::is_regular_file(path) && is_image_file(path);
    LOG_TRACE("is_image: %s -> %s", path.c_str(), result ? "yes" : "no");
    return result;
}

std::string get_hef_name(const std::string &path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// PREPROCESSING
// ─────────────────────────────────────────────────────────────────────────────

static cv::Mat center_crop(const cv::Mat &img, int target_h, int target_w) {
    // 짧은 변을 target에 맞게 리사이즈 후 중앙 크롭
    double scale = std::max(double(target_w) / img.cols, double(target_h) / img.rows);
    int new_w = int(img.cols * scale), new_h = int(img.rows * scale);
    cv::Mat resized;
    cv::resize(img, resized, cv::Size(new_w, new_h));
    const int x = (new_w - target_w) / 2;
    const int y = (new_h - target_h) / 2;
    return resized(cv::Rect(x, y, target_w, target_h)).clone();
}

void preprocess_image(const cv::Mat &src,
                      cv::Mat &dst,
                      uint32_t target_width,
                      uint32_t target_height,
                      bool image_normalization) {
    if (src.empty()) { dst = cv::Mat(); return; }
    LOG_TRACE("preprocess_image: src=[H:%d W:%d C:%d] target=[H:%u W:%u] norm=%d",
              src.rows, src.cols, src.channels(), target_height, target_width, image_normalization);
    cv::Mat rgb;
    switch (src.channels()) {
        case 3:  cv::cvtColor(src, rgb, cv::COLOR_BGR2RGB);  break;
        case 4:  cv::cvtColor(src, rgb, cv::COLOR_BGRA2RGB); break;
        case 1:  cv::cvtColor(src, rgb, cv::COLOR_GRAY2RGB); break;
        default: { std::vector<cv::Mat> ch(3, src); cv::merge(ch, rgb); cv::cvtColor(rgb, rgb, cv::COLOR_BGR2RGB); }
    }
    cv::Mat fitted = center_crop(rgb, static_cast<int>(target_height), static_cast<int>(target_width));
    if (image_normalization) {
        fitted.convertTo(dst, CV_32FC3, 1.0 / 255.0);
        if (!dst.isContinuous()) dst = dst.clone();
    } else {
        dst = fitted.isContinuous() ? fitted : fitted.clone();
    }
    LOG_TRACE("preprocess_image: dst=[H:%d W:%d C:%d type:%d]", dst.rows, dst.cols, dst.channels(), dst.type());
}

void preprocess_image_batch(const std::vector<cv::Mat> &org_frames,
                            std::vector<cv::Mat> &preprocessed_frames,
                            uint32_t target_width,
                            uint32_t target_height,
                            bool image_normalization) {
    LOG_TRACE("preprocess_image_batch: %zu frames target=[H:%u W:%u]", org_frames.size(), target_height, target_width);
    preprocessed_frames.resize(org_frames.size());
    for (size_t i = 0; i < org_frames.size(); ++i)
        preprocess_image(org_frames[i], preprocessed_frames[i], target_width, target_height, image_normalization);
    LOG_TRACE("preprocess_image_batch: done");
}




// ─────────────────────────────────────────────────────────────────────────────
// VISUALIZATION & SAVE
// ─────────────────────────────────────────────────────────────────────────────

cv::Mat resize_with_letterbox(
    const cv::Mat &src, 
    int target_w, int target_h) {

    if (src.empty()) return src;
    double scale = std::min(double(target_w) / src.cols, double(target_h) / src.rows);
    int new_w = int(src.cols * scale), new_h = int(src.rows * scale);
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h));
    cv::Mat canvas(target_h, target_w, src.type(), cv::Scalar(0,0,0));
    resized.copyTo(canvas(cv::Rect((target_w - new_w) / 2, (target_h - new_h) / 2, new_w, new_h)));
    return canvas;
}

void draw_label(
    cv::Mat &frame, 
    const std::string &label, 
    Corner corner, cv::Scalar color) {
    if (frame.empty() || label.empty()) return;

    const double font_scale = std::max(frame.cols, frame.rows) / 800.0;
    const int thickness     = std::max(1, static_cast<int>(font_scale * 1.5));
    const int font          = cv::FONT_HERSHEY_SIMPLEX;
    const int margin        = static_cast<int>(10 * font_scale);

    int baseline = 0;
    cv::Size ts  = cv::getTextSize(label, font, font_scale, thickness, &baseline);

    cv::Point org;
    switch (corner) {
        case Corner::TOP_LEFT:     org = {margin,                          margin + ts.height};           break;
        case Corner::TOP_RIGHT:    org = {frame.cols - ts.width - margin,  margin + ts.height};           break;
        case Corner::BOTTOM_LEFT:  org = {margin,                          frame.rows - margin};           break;
        case Corner::BOTTOM_RIGHT: org = {frame.cols - ts.width - margin,  frame.rows - margin};          break;
    }

    cv::putText(frame, label, org + cv::Point(1, 1), font, font_scale, cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
    cv::putText(frame, label, org, font, font_scale, color, thickness, cv::LINE_AA);
}

static std::pair<int,int> parse_resolution(const std::string &res) {
    auto pos = res.find('x');
    return { std::stoi(res.substr(0, pos)), std::stoi(res.substr(pos + 1)) };
}

bool show_frame(const std::string &window_name, cv::Mat &frame, const std::string &output_resolution) {
    cv::Mat display = frame;
    if (!output_resolution.empty() && !frame.empty()) {
        auto [w, h] = parse_resolution(output_resolution);
        display = resize_with_letterbox(frame, w, h);
    }
    cv::imshow(window_name, display);
    int key = cv::waitKey(1);
    return !(key == 'q' || key == 27);
}

void save_image(const std::string &output_path, 
                const cv::Mat &frame, 
                const std::string &output_resolution) {
    cv::Mat to_save = frame;
    if (!output_resolution.empty() && !frame.empty()) {
        auto [w, h] = parse_resolution(output_resolution);
        to_save = resize_with_letterbox(frame, w, h);
    }
    bool ok = cv::imwrite(output_path, to_save);
    if (ok)
        LOG_TRACE("save_image: saved %s [H:%d W:%d]", output_path.c_str(), to_save.rows, to_save.cols);
    else
        LOG_ERROR("save_image: failed to write %s", output_path.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// STATS
// ─────────────────────────────────────────────────────────────────────────────

static void show_progress_helper(size_t current, size_t total) {
    int pos = static_cast<int>(50 * (current + 1) / total);
    int pct = static_cast<int>(100.0f * (current + 1) / total);
    std::cout << "\rProgress: [";
    for (int j = 0; j < 50; ++j)
        std::cout << (j < pos ? '=' : j == pos ? '>' : ' ');
    std::cout << "] " << std::setw(3) << pct
              << "% (" << std::setw(3) << (current + 1) << "/" << total << ")" << std::flush;
}

void show_progress(InputType &input_type, int progress, size_t frame_count) {
    if (input_type.is_video || input_type.is_directory)
        show_progress_helper(progress, frame_count);
}

void print_inference_statistics(std::chrono::duration<double> inference_time,
                                const std::string &hef_file,
                                double frame_count,
                                std::chrono::duration<double> total_time)
{
    (void)hef_file;
    double fps     = frame_count / inference_time.count();
    double latency = 1000.0 / fps;
    LOG_INFO("stats: frames=%.0f fps=%.2f latency=%.2f ms total=%.3f sec",
             frame_count, fps, latency, total_time.count());
    std::cout << color::BOLDGREEN << "\n-I-----------------------------------------------\n"
              << "-I- Average FPS:  " << fps << "\n"
              << "-I- Total time:   " << inference_time.count() << " sec\n"
              << "-I- Latency:      " << latency << " ms\n"
              << "-I-----------------------------------------------\n" << color::RESET
              << color::BOLDBLUE << "-I- Total run time: " << total_time.count() << " sec\n" << color::RESET;
}

} // namespace hailo_utils
