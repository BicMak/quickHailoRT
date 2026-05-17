/**
 * Object detection on video (YOLO + built-in NMS) using HailoInfer + SAHI.
 *
 * Flow (per frame):
 *   VideoCapture.read → slice → preprocess → infer_batch → postprocess → VideoWriter.write
 *
 * Overlays EMA-smoothed per-frame time + FPS onto each output frame.
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
    auto cfg = parse_sahi_config(argc, argv, "config.yaml");
    logger::set_log_file(cfg.logs_dir.c_str());
    LOG_INFO("object_detection_video started");

    if (cfg.net.empty())   { LOG_ERROR("net path required");   return HAILO_INVALID_ARGUMENT; }
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

    // ── Open video ────────────────────────────────────────────────────────────
    cv::VideoCapture cap(cfg.input);
    if (!cap.isOpened()) {
        LOG_ERROR("cannot open video: %s", cfg.input.c_str());
        return HAILO_INVALID_ARGUMENT;
    }
    const int    in_w   = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int    in_h   = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double in_fps = cap.get(cv::CAP_PROP_FPS);
    const int    n_frames_total = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));

    // Skip frames so we process at ~TARGET_FPS regardless of input fps.
    constexpr double TARGET_FPS = 3.0;
    const int frame_stride = (in_fps > TARGET_FPS)
                             ? static_cast<int>(std::round(in_fps / TARGET_FPS))
                             : 1;
    LOG_INFO("video: %dx%d  fps=%.2f  frames=%d  stride=%d (process every %d-th frame)",
             in_w, in_h, in_fps, n_frames_total, frame_stride, frame_stride);

    // ── Open writer ───────────────────────────────────────────────────────────
    std::filesystem::path in_path(cfg.input);
    std::filesystem::path out_dir(cfg.output);
    std::filesystem::create_directories(out_dir);
    std::filesystem::path out_path = out_dir / (in_path.stem().string() + "_annotated.mp4");

    const double out_fps = TARGET_FPS;  // match the effective sampling rate
    cv::VideoWriter writer(out_path.string(),
                           cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                           out_fps, cv::Size(in_w, in_h));
    if (!writer.isOpened()) {
        LOG_ERROR("cannot open video writer: %s", out_path.c_str());
        return HAILO_INVALID_ARGUMENT;
    }
    LOG_INFO("writing: %s", out_path.c_str());

    // ── Pre-allocate buffers (reused across all frames) ──────────────────────
    constexpr size_t MAX_SLICES = 32;
    std::vector<cv::Mat>                 preprocessed(MAX_SLICES);
    std::vector<MemoryView>              inputs(MAX_SLICES);
    std::vector<std::vector<uint8_t>>    raw_bufs(MAX_SLICES, std::vector<uint8_t>(frame_bytes));
    std::vector<std::vector<MemoryView>> out_views(MAX_SLICES);
    for (size_t i = 0; i < MAX_SLICES; ++i)
        out_views[i] = { MemoryView(raw_bufs[i].data(), frame_bytes) };

    VisualizationParams vis;
    vis.score_thresh = cfg.threshold;

    std::vector<double> slice_ms_vec, prep_ms_vec, infer_ms_vec, postprocess_ms_vec, frame_total_ms;
    size_t processed = 0;

    // EMA smoothing for on-screen overlay (alpha closer to 1 = more reactive)
    constexpr double EMA_ALPHA = 0.1;
    double ema_total_ms = 0.0;
    bool   ema_init     = false;

    // Process full video (no time cap)
    const int max_input_frames = INT_MAX;

    cv::Mat frame;
    int frame_idx = -1;
    while (cap.read(frame)) {
        if (frame.empty()) break;
        ++frame_idx;
        if (frame_idx >= max_input_frames) break;
        // Skip frames to match TARGET_FPS sampling
        if (frame_idx % frame_stride != 0) continue;
        auto t_start = Clock::now();

        // ── Phase 1: Slice ────────────────────────────────────────────────────
        auto t0 = Clock::now();
        SliceImageResult sliced = slice_image_by_size(
            frame, static_cast<int>(target_h), static_cast<int>(target_w),
            cfg.overlap_height, cfg.overlap_width);
        const auto &slices = sliced.sliced_image_list;
        const size_t n = slices.size();
        if (n > MAX_SLICES) {
            LOG_ERROR("slice count %zu exceeds MAX_SLICES=%zu, skipping frame %zu",
                      n, MAX_SLICES, processed);
            continue;
        }
        double slice_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        // ── Phase 2: Input prep ───────────────────────────────────────────────
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
            LOG_ERROR("infer_batch failed at frame %zu", processed);
            continue;
        }

        // ── Phase 4: Postprocess ──────────────────────────────────────────────
        auto t3 = Clock::now();
        const float inv_w = 1.0f / static_cast<float>(frame.cols);
        const float inv_h = 1.0f / static_cast<float>(frame.rows);
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

        cv::Mat out_frame = frame.clone();
        draw_bounding_boxes(out_frame, final_bboxes, vis);
        double post_ms = std::chrono::duration<double, std::milli>(Clock::now() - t3).count();

        double total_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_start).count();

        // EMA update (first sample seeds directly)
        if (!ema_init) { ema_total_ms = total_ms; ema_init = true; }
        else           { ema_total_ms = EMA_ALPHA * total_ms + (1.0 - EMA_ALPHA) * ema_total_ms; }
        double ema_fps = ema_total_ms > 0.0 ? 1000.0 / ema_total_ms : 0.0;

        // ── Overlay timing onto frame ────────────────────────────────────────
        {
            char line[128];
            std::snprintf(line, sizeof(line), "%.1f ms/frame (EMA)   %.2f FPS",
                          ema_total_ms, ema_fps);

            const int font = cv::FONT_HERSHEY_SIMPLEX;
            const double scale = std::max(0.5, out_frame.cols / 1280.0);
            const int thickness = std::max(1, static_cast<int>(scale * 2));

            // Black shadow + white text for legibility on any background
            int y = static_cast<int>(30 * scale) + 10;
            cv::putText(out_frame, line, cv::Point(12, y + 2), font, scale,
                        cv::Scalar(0, 0, 0), thickness + 2, cv::LINE_AA);
            cv::putText(out_frame, line, cv::Point(10, y), font, scale,
                        cv::Scalar(255, 255, 255), thickness, cv::LINE_AA);
        }

        writer.write(out_frame);

        LOG_INFO("[sahi-vid] frame=%zu/%d slices=%zu slice=%.1f prep=%.1f infer=%.1f post=%.1f total=%.1f ms  ema=%.1f ms (%.2f FPS)  det=%zu->%zu",
                 processed + 1, n_frames_total, n,
                 slice_ms, prep_ms, inf_ms, post_ms, total_ms,
                 ema_total_ms, ema_fps, det_before, final_bboxes.size());

        slice_ms_vec.push_back(slice_ms);
        prep_ms_vec.push_back(prep_ms);
        infer_ms_vec.push_back(inf_ms);
        postprocess_ms_vec.push_back(post_ms);
        frame_total_ms.push_back(total_ms);
        ++processed;
    }

    cap.release();
    writer.release();

    // ── Summary ───────────────────────────────────────────────────────────────
    double total_pipeline_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t_total_start).count();

    LOG_INFO("==================== PERFORMANCE SUMMARY ====================");
    LOG_INFO("model load: %.2f ms", model_load_ms);
    LOG_INFO("--- VIDEO PIPELINE (%zu frames) ---", processed);
    log_stats("  slice                  ", slice_ms_vec);
    log_stats("  input prep (preprocess)", prep_ms_vec);
    log_stats("  infer      (all slices)", infer_ms_vec);
    log_stats("  postprocess (remap+draw)", postprocess_ms_vec);
    log_stats("  total/frame            ", frame_total_ms);
    LOG_INFO("  EMA total/frame (final): %.2f ms  (%.2f FPS)",
             ema_total_ms, ema_total_ms > 0.0 ? 1000.0 / ema_total_ms : 0.0);
    LOG_INFO("--- WHOLE PIPELINE ---");
    LOG_INFO("  total wall time: %.2f ms (%.2f s)",
             total_pipeline_ms, total_pipeline_ms / 1000.0);
    if (processed > 0)
        LOG_INFO("  overall throughput: %.2f frames/s",
                 1000.0 * processed / total_pipeline_ms);
    LOG_INFO("=============================================================");

    LOG_INFO("object_detection_video done");
    return HAILO_SUCCESS;
}
catch (const std::exception &e) {
    LOG_ERROR("exception: %s", e.what());
    return HAILO_INTERNAL_FAILURE;
}
