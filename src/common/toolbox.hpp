#ifndef _HAILO_COMMON_TOOLBOX_HPP_
#define _HAILO_COMMON_TOOLBOX_HPP_

#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include "hailo/hailort.h"
#include "hailo_infer.hpp"

using Clock = std::chrono::steady_clock;

namespace hailo_utils {

    namespace fs = std::filesystem;

    namespace color {
        inline constexpr const char* RESET       = "\033[0m";
        inline constexpr const char* MAGENTA     = "\033[35m";
        inline constexpr const char* BOLDGREEN   = "\033[1m\033[32m";
        inline constexpr const char* BOLDBLUE    = "\033[1m\033[34m";
        inline constexpr const char* BOLDMAGENTA = "\033[1m\033[35m";
    }

    extern std::vector<cv::Scalar> COLORS;
    extern const std::unordered_map<std::string, std::pair<int,int>> RESOLUTION_MAP;

    struct InputType {
        bool is_image     = false;
        bool is_video     = false;
        bool is_directory = false;
        bool is_camera    = false;
    };

    struct CommandLineArgs {
        std::string net;
        std::string input_dir;
        std::string output_dir;

        bool   save_stream_output;
        bool   no_display;
        size_t batch_size;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // FILE / PATH
    // ─────────────────────────────────────────────────────────────────────────────
    bool is_image_file(const std::string &path);
    bool is_directory_of_images(const std::string &path, size_t &entry_count, size_t batch_size);
    bool is_image(const std::string &path);

    std::string get_hef_name(const std::string &path);


    // ─────────────────────────────────────────────────────────────────────────────
    // PREPROCESSING
    // ─────────────────────────────────────────────────────────────────────────────
    // 현재 구현에서는 반환자체를 이미지를 복제해서 주도록 되어있음
    // 만약에 제로카피 파이프라인을 구축할거면 별도로 구현해야함
    // BGR/BGRA/GRAY → RGB, pad/crop to target size (1:1 in-place copy)
    
    void preprocess_image(const cv::Mat &src,
                          cv::Mat &dst,
                          uint32_t target_width,
                          uint32_t target_height,
                          bool image_normalization);

    // batch overload
    void preprocess_image_batch(const std::vector<cv::Mat> &org_frames,
                           std::vector<cv::Mat> &preprocessed_frames,
                           uint32_t target_width,
                           uint32_t target_height,
                           bool image_normalization);


    // ─────────────────────────────────────────────────────────────────────────────
    // VISUALIZATION & SAVE
    // ─────────────────────────────────────────────────────────────────────────────
    cv::Mat resize_with_letterbox(
        const cv::Mat &src,
        int target_w,
        int target_h);

    enum class Corner { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT };

    void draw_label(cv::Mat &frame,
                    const std::string &label,
                    Corner corner = Corner::TOP_LEFT,
                    cv::Scalar color = {255, 255, 255});

    // imshow로 표시, 'q'/ESC 시 false 반환
    bool show_frame(const std::string &window_name, cv::Mat &frame,
                    const std::string &output_resolution = "");

    // 이미지 파일로 저장
    void save_image(const std::string &output_path, const cv::Mat &frame,
                    const std::string &output_resolution = "");

    // ─────────────────────────────────────────────────────────────────────────────
    // STATS
    // ─────────────────────────────────────────────────────────────────────────────
    void show_progress(InputType &input_type, int progress, size_t frame_count);
    void print_inference_statistics(std::chrono::duration<double> inference_time,
                                    const std::string &hef_file,
                                    double frame_count,
                                    std::chrono::duration<double> total_time);
}

#endif
