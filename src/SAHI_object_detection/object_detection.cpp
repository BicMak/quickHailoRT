/**
 * Object detection (YOLO + built-in NMS) using HailoInfer + SAHI.
 *
 * Flow (per image):
 *   slice_image_by_size → preprocess slices → infer_batch
 */

#include "hailo_infer.hpp"
#include "cli.hpp"
#include "toolbox.hpp"
#include "../common/logging.hpp"
#include "slicing.h"

#include <opencv2/opencv.hpp>
#include <numeric>
#include <chrono>

using namespace hailort;
using namespace hailo_utils;
using Clock = std::chrono::steady_clock;


static void log_stats(const char *label, const std::vector<double> &ms) {
    if (ms.empty()) { LOG_INFO("%s: no samples", label); return; }
    double sum = std::accumulate(ms.begin(), ms.end(), 0.0);
    double mn  = *std::min_element(ms.begin(), ms.end());
    double mx  = *std::max_element(ms.begin(), ms.end());
    double avg = sum / ms.size();
    LOG_INFO("%s: n=%zu  total=%.2f ms  avg=%.2f ms  min=%.2f ms  max=%.2f ms",
             label, ms.size(), sum, avg, mn, mx);
}

int main(int argc, char **argv) try {
    logger::set_log_file("logs");
    LOG_INFO("object_detection started");

    auto cfg = parse_sahi_config(argc, argv, "config.yaml");

    if (cfg.net.empty()){
        LOG_ERROR("net path required");
        return HAILO_INVALID_ARGUMENT;
    }
    if (cfg.input.empty()) { LOG_ERROR("input path required"); return HAILO_INVALID_ARGUMENT; }

    auto t_total_start = Clock::now();

    // ── Model load ────────────────────────────────────────────────────────────
    auto t_load = Clock::now();
    HailoInfer model(cfg.net, HAILO_FORMAT_TYPE_UINT8, HAILO_FORMAT_TYPE_FLOAT32);
    double model_load_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_load).count();
    LOG_INFO("[TIMING] model load: %.2f ms", model_load_ms);

    auto input_info   = model.get_input_info();
    auto output_info  = model.get_output_infos()[0];
    const uint32_t target_w     = input_info.shape.width;
    const uint32_t target_h     = input_info.shape.height;
    const size_t   frame_bytes  = model.get_output_frame_size(0);
    const size_t   class_count  = output_info.nms_shape.number_of_classes;
    LOG_INFO("model: input=[H:%u W:%u]  classes=%zu  nms_output=%zu bytes",
             target_h, target_w, class_count, frame_bytes);

    // ── Collect input paths ───────────────────────────────────────────────────
    std::vector<std::filesystem::path> all_paths;
    if (std::filesystem::is_directory(cfg.input)) {
        for (const auto &entry : std::filesystem::directory_iterator(cfg.input))
            if (is_image_file(entry.path().string()))
                all_paths.push_back(entry.path());
    } else if (is_image_file(cfg.input)) {
        all_paths.push_back(cfg.input);
    } else {
        LOG_ERROR("input is not an image or directory: %s", cfg.input.c_str());
        return HAILO_INVALID_ARGUMENT;
    }
    LOG_INFO("found %zu image(s)", all_paths.size());

    // ── Pre-allocate buffers (reused across all images) ──────────────────────
    constexpr size_t MAX_SLICES = 32;
    std::vector<cv::Mat>                 preprocessed(MAX_SLICES);
    std::vector<MemoryView>              inputs(MAX_SLICES);
    std::vector<std::vector<uint8_t>>    raw_bufs(MAX_SLICES, std::vector<uint8_t>(frame_bytes));
    std::vector<std::vector<MemoryView>> out_views(MAX_SLICES);
    for (size_t i = 0; i < MAX_SLICES; ++i)
        out_views[i] = { MemoryView(raw_bufs[i].data(), frame_bytes) };

    // ── Per-image loop ────────────────────────────────────────────────────────
    VisualizationParams vis;
    vis.score_thresh = cfg.threshold;

    std::vector<double> slice_ms_vec, prep_ms_vec, infer_ms_vec, postprocess_ms_vec, frame_total_ms;
    size_t processed = 0;

    for (const auto &img_path : all_paths) {
        auto t_start = Clock::now();

        const cv::Mat org = cv::imread(img_path.string());
        if (org.empty()) {
            LOG_WARN("cannot read: %s", img_path.filename().c_str());
            continue;
        }

        // ── Phase 1: Slice ────────────────────────────────────────────────────
        auto t0 = Clock::now();
        SliceImageResult sliced = slice_image_by_size(
            org, static_cast<int>(target_h), static_cast<int>(target_w),
            cfg.overlap_height, cfg.overlap_width);
        const auto &slices = sliced.sliced_image_list;
        const size_t n = slices.size();
        if (n > MAX_SLICES) {
            LOG_ERROR("slice count %zu exceeds MAX_SLICES=%zu, skipping %s",
                      n, MAX_SLICES, img_path.filename().c_str());
            continue;
        }
        double slice_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        // ── Phase 2: Input prep (preprocess + memcopy to MemoryView) ─────────
        auto t1 = Clock::now();
        for (size_t i = 0; i < n; ++i) {
            preprocess_image(slices[i].image, preprocessed[i], target_w, target_h, false);
            inputs[i] = MemoryView(preprocessed[i].data,
                                   preprocessed[i].total() * preprocessed[i].elemSize());
        }
        double prep_ms = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();

        // ── Phase 3: Inference ────────────────────────────────────────────────
        auto t2 = Clock::now();
        std::vector<MemoryView>              inputs_n(inputs.begin(), inputs.begin() + n);
        std::vector<std::vector<MemoryView>> out_views_n(out_views.begin(), out_views.begin() + n);
        auto status = model.infer_batch(inputs_n, out_views_n);
        double inf_ms = std::chrono::duration<double, std::milli>(Clock::now() - t2).count();
        if (status != HAILO_SUCCESS) {
            LOG_ERROR("infer_batch failed for %s", img_path.filename().c_str());
            continue;
        }

        // ── Phase 4: Postprocess (coord remap + draw + save) ─────────────────
        auto t3 = Clock::now();
        const float inv_w = 1.0f / static_cast<float>(org.cols);
        const float inv_h = 1.0f / static_cast<float>(org.rows);
        std::vector<NamedBbox> all_bboxes;
        all_bboxes.reserve(n * 100);
        for (size_t i = 0; i < n; ++i) {
            int ox = slices[i].starting_pixel[0];
            int oy = slices[i].starting_pixel[1];
            for (auto &nb : parse_nms_data(raw_bufs[i].data(), class_count)) {
                if (nb.bbox.score < cfg.threshold) continue;
                nb.bbox.x_min = (nb.bbox.x_min * target_w + ox) * inv_w;
                nb.bbox.y_min = (nb.bbox.y_min * target_h + oy) * inv_h;
                nb.bbox.x_max = (nb.bbox.x_max * target_w + ox) * inv_w;
                nb.bbox.y_max = (nb.bbox.y_max * target_h + oy) * inv_h;
                all_bboxes.push_back(nb);
            }
        }

        size_t det_before = all_bboxes.size();
        std::vector<NamedBbox> final_bboxes = apply_nmm(all_bboxes, cfg.nmm_iou_threshold);

        cv::Mat out_frame = org.clone();
        draw_bounding_boxes(out_frame, final_bboxes, vis);
        double post_ms = std::chrono::duration<double, std::milli>(Clock::now() - t3).count();

        auto out_path = std::filesystem::path(cfg.output) / img_path.filename();
        save_image(out_path.string(), out_frame);

        double total_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_start).count();
        LOG_INFO("[sahi] %s: slices=%zu  slice=%.1f ms  prep=%.1f ms  infer=%.1f ms  post=%.1f ms  total=%.1f ms  det=%zu→%zu",
                 img_path.filename().c_str(), n, slice_ms, prep_ms, inf_ms, post_ms, total_ms, det_before, final_bboxes.size());

        slice_ms_vec.push_back(slice_ms);
        prep_ms_vec.push_back(prep_ms);
        infer_ms_vec.push_back(inf_ms);
        postprocess_ms_vec.push_back(post_ms);
        frame_total_ms.push_back(total_ms);
        ++processed;
    }

    // ── Summary ───────────────────────────────────────────────────────────────
    double total_pipeline_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t_total_start).count();

    LOG_INFO("==================== PERFORMANCE SUMMARY ====================");
    LOG_INFO("model load: %.2f ms", model_load_ms);
    LOG_INFO("--- IMAGE PIPELINE (%zu images) ---", processed);
    log_stats("  slice                  ", slice_ms_vec);
    log_stats("  input prep (preprocess)", prep_ms_vec);
    log_stats("  infer      (all slices)", infer_ms_vec);
    log_stats("  postprocess (remap+draw)", postprocess_ms_vec);
    log_stats("  total/image            ", frame_total_ms);
    LOG_INFO("--- WHOLE PIPELINE ---");
    LOG_INFO("  total wall time: %.2f ms (%.2f s)",
             total_pipeline_ms, total_pipeline_ms / 1000.0);
    if (processed > 0)
        LOG_INFO("  overall throughput: %.2f images/s",
                 1000.0 * processed / total_pipeline_ms);
    LOG_INFO("=============================================================");

    LOG_INFO("object_detection done");
    return HAILO_SUCCESS;
}
catch (const std::exception &e) {
    LOG_ERROR("exception: %s", e.what());
    return HAILO_INTERNAL_FAILURE;
}