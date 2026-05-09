/**
 * Copyright (c) 2020-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
#include "toolbox.hpp"
#include "hailo_infer.hpp"
#include "hailo/hailort.hpp"
#include "imagenet_labels.hpp"
#include "../common/logging.hpp"

#include <iostream>
#include <chrono>
#include <opencv2/opencv.hpp>

using namespace hailo_utils;
using Clock = std::chrono::steady_clock;

static bool APPLY_SOFTMAX = false;

template <typename T>
static int argmax_vec(const std::vector<T> &v) {
    return static_cast<int>(std::distance(v.begin(), std::max_element(v.begin(), v.end())));
}

template <typename T>
static std::vector<float> softmax_vec(const std::vector<T> &v) {
    std::vector<float> out;
    out.reserve(v.size());
    float m = -INFINITY;
    for (auto &x : v) m = std::max<float>(m, static_cast<float>(x));
    float sum = 0.0f;
    for (auto &x : v) sum += std::exp(static_cast<float>(x) - m);
    for (auto &x : v) out.push_back(std::exp(static_cast<float>(x) - m) / sum);
    return out;
}

static std::string classify_and_format(const std::vector<float> &logits, float threshold = 0.20f) {
    static ImageNetLabels labels;
    std::vector<float> probs = APPLY_SOFTMAX
        ? softmax_vec(logits)
        : std::vector<float>(logits.begin(), logits.end());

    int idx = APPLY_SOFTMAX ? argmax_vec(probs) : argmax_vec(logits);
    float conf = probs.empty() ? 0.0f : probs[idx];

    LOG_TRACE("classify: top_idx=%d conf=%.4f threshold=%.2f", idx, conf, threshold);

    if (conf < threshold) {
        LOG_TRACE("classify: below threshold, returning N/A");
        return std::string("N/A");
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << conf * 100.0f;
    std::string label = labels.imagenet_labelstring(idx) + " (" + oss.str() + "%)";
    LOG_TRACE("classify: result=%s", label.c_str());
    return label;
}

int main()
{
    try {
        logger::set_log_file("logs");
        LOG_INFO("classifier started");

        CommandLineArgs args;
        args.net        = "src/classification/hef/efficientnet_m.hef";
        args.input_dir  = "local_data/input_img";
        args.output_dir = "local_data/output_img";
        LOG_DEBUG("config: net=%s", args.net.c_str());
        LOG_DEBUG("config: input=%s",  args.input_dir.c_str());
        LOG_DEBUG("config: output=%s", args.output_dir.c_str());

        auto t_start = Clock::now();

        LOG_INFO("loading model: %s", args.net.c_str());
        HailoInfer model(args.net, HAILO_FORMAT_TYPE_UINT8, HAILO_FORMAT_TYPE_FLOAT32);
        auto input_info = model.get_input_info();
        const uint32_t target_w = input_info.shape.width;
        const uint32_t target_h = input_info.shape.height;
        LOG_INFO("model ready: input=[H:%u W:%u]", target_h, target_w);

        std::filesystem::create_directories(args.output_dir);

        std::chrono::duration<double> inference_time{0};
        size_t processed = 0;

        for (const auto &entry : std::filesystem::directory_iterator(args.input_dir)) {
            if (!is_image_file(entry.path().string())) continue;

            LOG_DEBUG("processing: %s", entry.path().filename().c_str());

            cv::Mat org = cv::imread(entry.path().string());
            if (org.empty()) {
                LOG_WARN("failed to read image, skipping: %s", entry.path().c_str());
                std::cerr << "skip: " << entry.path() << "\n";
                continue;
            }
            LOG_TRACE("image loaded: [H:%d W:%d C:%d]", org.rows, org.cols, org.channels());

            cv::Mat preprocessed;
            preprocess_image(org, preprocessed, target_w, target_h, false);
            LOG_TRACE("image preprocessed: [H:%d W:%d C:%d]", preprocessed.rows, preprocessed.cols, preprocessed.channels());

            std::vector<std::vector<uint8_t>> output_buffers;
            auto t0 = Clock::now();
            auto results = model.infer(preprocessed, output_buffers);
            double elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            inference_time += Clock::now() - t0;
            LOG_DEBUG("infer done: %.2f ms", elapsed_ms);

            if (!results.empty()) {
                const auto &[data_ptr, info] = results[0];
                const size_t num_classes = static_cast<size_t>(info.shape.features);
                const float *f = reinterpret_cast<const float*>(data_ptr);
                std::vector<float> probs(f, f + num_classes);
                LOG_TRACE("output: num_classes=%zu", num_classes);

                std::string label = classify_and_format(probs, 0.30f);
                LOG_INFO("result: %s -> %s", entry.path().filename().c_str(), label.c_str());
                draw_label(org, label, Corner::TOP_LEFT);
            }

            const std::string out_path = args.output_dir + "/" + entry.path().filename().string();
            save_image(out_path, org);
            LOG_TRACE("saved: %s", out_path.c_str());
            ++processed;
        }

        LOG_INFO("all done: %zu images processed", processed);
        if (processed > 0)
            print_inference_statistics(inference_time, args.net,
                                       static_cast<double>(processed), Clock::now() - t_start);
        return HAILO_SUCCESS;
    }
    catch (const std::exception &e) {
        LOG_ERROR("exception: %s", e.what());
        std::cerr << "ERROR: " << e.what() << "\n";
        return HAILO_INTERNAL_FAILURE;
    }
}
