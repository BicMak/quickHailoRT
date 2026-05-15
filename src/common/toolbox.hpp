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

    struct VisualizationParams {
        float score_thresh      = 0.50f;
        int   max_boxes_to_draw = 0;    // 0 = no limit
    };

    // ─────────────────────────────────────────────────────────────────────────────
    // OBJECT DETECTION
    // ─────────────────────────────────────────────────────────────────────────────

    struct NamedBbox {
        hailo_bbox_float32_t bbox;
        size_t class_id;
    };

    cv::Rect get_bbox_coordinates(const hailo_bbox_float32_t &bbox,
                                  int frame_width, int frame_height);
    cv::Rect get_bbox_coordinates_sahi(const hailo_bbox_float32_t &bbox,
                                       int slice_width, int slice_height,
                                       int offset_x,    int offset_y);
    void draw_label(cv::Mat &frame, const std::string &label,
                    const cv::Point &top_left, const cv::Scalar &color);
    void draw_single_bbox(cv::Mat &frame, const NamedBbox &named_bbox,
                          const cv::Scalar &color);
    void draw_bounding_boxes(cv::Mat &frame, const std::vector<NamedBbox> &bboxes,
                             const VisualizationParams &vis);
    std::vector<NamedBbox> parse_nms_data(uint8_t *data, size_t max_class_count);
    void initialize_class_colors(std::unordered_map<int, cv::Scalar> &class_colors);

    // Cross-slice Non-Maximum Merging: groups overlapping boxes of the same class
    // by IoU threshold and merges each group into one score-weighted average box.
    std::vector<NamedBbox> apply_nmm(const std::vector<NamedBbox> &bboxes,
                                     float iou_threshold = 0.5f);

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
