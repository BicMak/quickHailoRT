/**
 * Copyright (c) 2020-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
#include "toolbox.hpp"
#include "hailo_infer.hpp"
#include "cli.hpp"
#include "hailo/hailort.hpp"
#include "imagenet_labels.hpp"
#include "../common/logging.hpp"

#include <chrono>
#include <opencv2/opencv.hpp>

using namespace hailo_utils;
using Clock = std::chrono::steady_clock;

template <typename T>
static int argmax_vec(const std::vector<T> &v) {
    return static_cast<int>(std::distance(v.begin(), std::max_element(v.begin(), v.end())));
}

static std::string classify_and_format(const std::vector<float> &probs, float threshold) {
    static ImageNetLabels labels;
    int idx     = argmax_vec(probs);
    float conf  = probs.empty() ? 0.0f : probs[idx];

    LOG_TRACE("classify: top_idx=%d conf=%.4f threshold=%.2f", idx, conf, threshold);

    if (conf < threshold || idx < 0 || idx >= 1000)
        return "N/A";

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << conf * 100.0f;
    return labels.imagenet_labelstring(idx) + " (" + oss.str() + "%)";
}

int main(int argc, char **argv)
{
    try {
        logger::set_log_file("logs");
        LOG_INFO("classifier started");

        auto cfg = parse_config(argc, argv, "src/classification/config.yaml");
        LOG_DEBUG("config: net=%s input=%s output=%s threshold=%.2f",
                  cfg.net.c_str(), cfg.input.c_str(), cfg.output.c_str(), cfg.threshold);

        auto t_start = Clock::now();

        HailoInfer model(cfg.net, HAILO_FORMAT_TYPE_UINT8, HAILO_FORMAT_TYPE_FLOAT32);
        auto input_info    = model.get_input_info();
        const uint32_t target_w = input_info.shape.width;
        const uint32_t target_h = input_info.shape.height;
        LOG_INFO("model ready: input=[H:%u W:%u]", target_h, target_w);

        std::filesystem::create_directories(cfg.output);

        std::chrono::duration<double> inference_time{0};
        size_t processed = 0;

        for (const auto &entry : std::filesystem::directory_iterator(cfg.input)) {
            if (!is_image_file(entry.path().string())) continue;

            LOG_DEBUG("processing: %s", entry.path().filename().c_str());

            cv::Mat org = cv::imread(entry.path().string());
            if (org.empty()) {
                LOG_WARN("failed to read image, skipping: %s", entry.path().c_str());
                continue;
            }

            cv::Mat preprocessed;
            preprocess_image(org, preprocessed, target_w, target_h, false);

            // allocate output buffers — size from vstream frame size (respects output_type)
            auto output_infos = model.get_output_infos();
            std::vector<std::vector<uint8_t>> raw_buffers(output_infos.size());
            std::vector<hailort::MemoryView>  output_buffers;
            size_t elem_size = (model.get_output_type() == HAILO_FORMAT_TYPE_FLOAT32) ? sizeof(float)
                           : (model.get_output_type() == HAILO_FORMAT_TYPE_UINT16)  ? sizeof(uint16_t)
                           : sizeof(uint8_t);
            for (size_t i = 0; i < output_infos.size(); ++i) {
                const auto &s = output_infos[i].shape;
                size_t sz = s.height * s.width * s.features * elem_size;
                raw_buffers[i].resize(sz);
                output_buffers.emplace_back(raw_buffers[i].data(), sz);
            }

            auto t0     = Clock::now();
            auto status = model.infer(
                hailort::MemoryView(preprocessed.data, preprocessed.total() * preprocessed.elemSize()),
                output_buffers);
            inference_time += Clock::now() - t0;
            LOG_DEBUG("infer done: %.2f ms",
                      std::chrono::duration<double, std::milli>(Clock::now() - t0).count());

            if (status == HAILO_SUCCESS && !raw_buffers.empty()) {
                const auto &info         = output_infos[0];
                const size_t num_classes = static_cast<size_t>(info.shape.features);
                const float *f           = reinterpret_cast<const float*>(raw_buffers[0].data());
                std::vector<float> probs(f, f + num_classes);

                std::string label = classify_and_format(probs, cfg.threshold);
                LOG_INFO("result: %s -> %s", entry.path().filename().c_str(), label.c_str());
                draw_label(org, label, Corner::TOP_LEFT);
            }

            save_image(cfg.output + "/" + entry.path().filename().string(), org);
            ++processed;
        }

        LOG_INFO("all done: %zu images processed", processed);
        if (processed > 0)
            print_inference_statistics(inference_time, cfg.net,
                                       static_cast<double>(processed), Clock::now() - t_start);
        return HAILO_SUCCESS;
    }
    catch (const std::exception &e) {
        LOG_ERROR("exception: %s", e.what());
        return HAILO_INTERNAL_FAILURE;
    }
}
