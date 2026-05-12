/**
 * Zero-shot classification (CLIP ViT-B/16) using HailoInfer.
 *
 * Flow:
 *   1. Tokenize prompts (BPE) → embedding lookup (.npy) → text_encoder.hef
 *      → EOT row → text_projection (.npy) → L2-normalize  →  text embeddings
 *   2. For each input image: preprocess → image_encoder.hef → L2-normalize
 *      → dot(text_embeddings) → softmax → argmax
 *
 * Embedding/projection dimensions are read from the .npy shape at runtime, so
 * the same code works for ViT-B/16 (dim=512) and ViT-L/14 (dim=768) as long as
 * the HEF and .npy files are matched.
 */

#include "hailo_infer.hpp"
#include "cli.hpp"
#include "toolbox.hpp"
#include "../common/logging.hpp"
#include "tokenizer/nn_embeddings.hpp"
#include "tokenizer/cnpy.h"

#include <opencv2/opencv.hpp>
#include <numeric>
#include <chrono>

using namespace hailort;
using Clock = std::chrono::steady_clock;

static constexpr float LOGIT_SCALE = 4.6051702f;

static void normalize_vec(std::vector<float> &v) {
    float n = std::sqrt(std::inner_product(v.begin(), v.end(), v.begin(), 0.0f));
    if (n > 0.0f)
        std::transform(v.begin(), v.end(), v.begin(), [n](float x){ return x / n; });
}

static std::vector<float> softmax(const std::vector<float> &x) {
    std::vector<float> e(x.size());
    float mx = *std::max_element(x.begin(), x.end());
    float sum = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) { e[i] = std::exp(x[i] - mx); sum += e[i]; }
    for (auto &v : e) v /= sum;
    return e;
}

// Loads [embed_dim, proj_dim] projection matrix from .npy (row-major).
static std::vector<float> load_text_projection(const std::string &path,
                                               size_t &embed_dim,
                                               size_t &proj_dim) {
    cnpy::NpyArray arr = cnpy::npy_load(path);
    if (arr.shape.size() != 2)
        throw std::runtime_error("text_projection npy must be 2D [embed_dim, proj_dim]");
    embed_dim = arr.shape[0];
    proj_dim  = arr.shape[1];
    const float *data = arr.data<float>();
    return std::vector<float>(data, data + embed_dim * proj_dim);
}

// gathered[embed_dim] @ projection[embed_dim x proj_dim] → out[proj_dim], then L2-normalize
static std::vector<float> project_and_normalize(
    const std::vector<float> &gathered,
    const std::vector<float> &projection,
    size_t embed_dim, size_t proj_dim) {

    std::vector<float> out(proj_dim, 0.0f);
    for (size_t j = 0; j < proj_dim; ++j) {
        float acc = 0.0f;
        for (size_t k = 0; k < embed_dim; ++k)
            acc += gathered[k] * projection[k * proj_dim + j];
        out[j] = acc;
    }
    normalize_vec(out);
    return out;
}

struct TextEncodeStats {
    std::vector<double> per_prompt_ms;
    double total_pipeline_ms = 0.0;
};

struct ImageInferResult {
    std::string label;
    float       confidence = 0.0f;
    double      preprocess_ms  = 0.0;
    double      infer_ms       = 0.0;
    double      postprocess_ms = 0.0;
};

static ImageInferResult classify_image(
    const std::filesystem::path &path,
    HailoInfer &model, size_t model_index,
    const std::vector<std::string> &prompts,
    const std::vector<std::vector<float>> &text_embeddings,
    uint32_t target_w, uint32_t target_h,
    size_t img_emb_count,
    std::vector<uint8_t> &img_out_raw) {

    cv::Mat org = cv::imread(path.string());
    if (org.empty())
        throw std::runtime_error("cannot read image: " + path.string());

    auto t0 = Clock::now();
    cv::Mat preprocessed;
    hailo_utils::preprocess_image(org, preprocessed, target_w, target_h, false);
    auto t1 = Clock::now();

    std::vector<MemoryView> outs{ MemoryView(img_out_raw.data(), img_out_raw.size()) };
    auto status = model.infer(
        MemoryView(preprocessed.data, preprocessed.total() * preprocessed.elemSize()),
        outs, model_index);
    auto t2 = Clock::now();
    if (status != HAILO_SUCCESS)
        throw std::runtime_error("image infer failed");

    const float *f = reinterpret_cast<const float*>(img_out_raw.data());
    std::vector<float> img_emb(f, f + img_emb_count);
    normalize_vec(img_emb);
    auto logits = dot_logits(img_emb, text_embeddings);
    auto probs  = softmax(logits);
    size_t best = std::distance(probs.begin(), std::max_element(probs.begin(), probs.end()));
    auto t3 = Clock::now();

    return {
        prompts[best],
        probs[best],
        std::chrono::duration<double, std::milli>(t1 - t0).count(),
        std::chrono::duration<double, std::milli>(t2 - t1).count(),
        std::chrono::duration<double, std::milli>(t3 - t2).count(),
    };
}

static std::vector<std::vector<float>> encode_texts(
    HailoInfer &model,
    size_t model_index,
    const std::vector<std::string> &prompts,
    const std::string &embedding_npy,
    const std::string &bpe_vocab,
    const std::string &text_projection_path,
    TextEncodeStats &stats) {

    auto t_pipeline_start = Clock::now();

    auto [token_embeddings, last_tokens] =
        tokenizer::get_hailo_input(prompts, embedding_npy, bpe_vocab);

    size_t proj_in = 0, proj_out = 0;
    auto projection = load_text_projection(text_projection_path, proj_in, proj_out);
    LOG_INFO("text_projection loaded: [%zu, %zu]", proj_in, proj_out);

    auto out_infos = model.get_output_infos(model_index);
    const auto &info = out_infos[0];
    const size_t out_count = (size_t)info.shape.height * info.shape.width * info.shape.features;
    const size_t encoder_dim = info.shape.features;
    if (encoder_dim != proj_in)
        throw std::runtime_error("text encoder output dim != text_projection input dim");

    std::vector<uint8_t> raw_out(out_count * sizeof(float));
    std::vector<std::vector<float>> text_embeddings;
    text_embeddings.reserve(prompts.size());
    stats.per_prompt_ms.reserve(prompts.size());

    for (size_t i = 0; i < token_embeddings.size(); ++i) {
        std::vector<MemoryView> outs{ MemoryView(raw_out.data(), raw_out.size()) };

        auto t_infer_start = Clock::now();
        auto status = model.infer(
            MemoryView(token_embeddings[i].data(),
                       token_embeddings[i].size() * sizeof(float)),
            outs, model_index);
        auto t_infer_end = Clock::now();
        if (status != HAILO_SUCCESS)
            throw std::runtime_error("text encoder infer failed");

        double infer_ms = std::chrono::duration<double, std::milli>(t_infer_end - t_infer_start).count();
        stats.per_prompt_ms.push_back(infer_ms);
        LOG_TRACE("[text %zu/%zu] \"%s\"  infer=%.2f ms",
                  i + 1, token_embeddings.size(), prompts[i].c_str(), infer_ms);

        const float *f = reinterpret_cast<const float*>(raw_out.data());
        const int row = last_tokens[i] - 1;
        std::vector<float> gathered(f + row * encoder_dim,
                                    f + (row + 1) * encoder_dim);
        text_embeddings.push_back(
            project_and_normalize(gathered, projection, proj_in, proj_out));
    }

    stats.total_pipeline_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t_pipeline_start).count();
    return text_embeddings;
}

// helper: min/avg/max from a vector of ms timings
static void log_stats(const char *label, const std::vector<double> &ms) {
    if (ms.empty()) { LOG_INFO("%s: no samples", label); return; }
    double sum = std::accumulate(ms.begin(), ms.end(), 0.0);
    double mn  = *std::min_element(ms.begin(), ms.end());
    double mx  = *std::max_element(ms.begin(), ms.end());
    double avg = sum / ms.size();
    LOG_INFO("%s: n=%zu  total=%.2f ms  avg=%.2f ms  min=%.2f ms  max=%.2f ms  throughput=%.2f /s",
             label, ms.size(), sum, avg, mn, mx, 1000.0 / avg);
}

static std::vector<float> dot_logits(const std::vector<float> &img_emb,
                                     const std::vector<std::vector<float>> &text_embs) {
    const float scale = std::exp(LOGIT_SCALE);
    std::vector<float> out(text_embs.size());
    for (size_t i = 0; i < text_embs.size(); ++i) {
        out[i] = std::inner_product(img_emb.begin(), img_emb.end(),
                                    text_embs[i].begin(), 0.0f) * scale;
    }
    return out;
}

int main(int argc, char **argv) try {
    logger::set_log_file("logs");
    LOG_INFO("zero_shot_classification started");

    auto cfg = hailo_utils::parse_zeroshot_config(argc, argv, "config.yaml");

    if (cfg.text_encoder.empty() || cfg.image_encoder.empty()) {
        LOG_ERROR("text_encoder and image_encoder paths required");
        return HAILO_INVALID_ARGUMENT;
    }
    if (cfg.prompts.empty()) {
        LOG_ERROR("no prompts provided");
        return HAILO_INVALID_ARGUMENT;
    }

    LOG_INFO("loading models — text: %s | image: %s",
             cfg.text_encoder.c_str(), cfg.image_encoder.c_str());

    auto t_total_start = Clock::now();
    auto t_load_start  = Clock::now();
    HailoInfer model({
        { cfg.text_encoder,  HAILO_FORMAT_TYPE_FLOAT32, HAILO_FORMAT_TYPE_FLOAT32 },
        { cfg.image_encoder, HAILO_FORMAT_TYPE_UINT8,   HAILO_FORMAT_TYPE_FLOAT32 },
    });
    const double model_load_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t_load_start).count();
    LOG_INFO("[TIMING] model load: %.2f ms", model_load_ms);

    constexpr size_t TEXT_IDX  = 0;
    constexpr size_t IMAGE_IDX = 1;

    LOG_INFO("encoding %zu prompts", cfg.prompts.size());
    TextEncodeStats text_stats;
    auto text_embeddings = encode_texts(model, TEXT_IDX, cfg.prompts,
                                        cfg.embedding_npy, cfg.bpe_vocab,
                                        cfg.text_projection, text_stats);
    LOG_INFO("text encoding done (%zu embeddings, dim=%zu)",
             text_embeddings.size(),
             text_embeddings.empty() ? 0 : text_embeddings[0].size());

    auto img_in_info  = model.get_input_info(IMAGE_IDX);
    auto img_out_info = model.get_output_infos(IMAGE_IDX)[0];
    const uint32_t target_w = img_in_info.shape.width;
    const uint32_t target_h = img_in_info.shape.height;
    const size_t   img_emb_count = (size_t)img_out_info.shape.height *
                                   img_out_info.shape.width *
                                   img_out_info.shape.features;

    std::vector<uint8_t> img_out_raw(img_emb_count * sizeof(float));

    std::vector<double> preprocess_ms, image_infer_ms, postprocess_ms, image_total_ms;
    size_t processed = 0;

    auto run_one = [&](const std::filesystem::path &path) {
        auto t_start = Clock::now();
        ImageInferResult r;
        try {
            r = classify_image(path, model, IMAGE_IDX, cfg.prompts, text_embeddings,
                               target_w, target_h, img_emb_count, img_out_raw);
        } catch (const std::exception &e) {
            LOG_WARN("skip %s: %s", path.filename().c_str(), e.what());
            return;
        }
        double total_ms = std::chrono::duration<double, std::milli>(Clock::now() - t_start).count();

        preprocess_ms.push_back(r.preprocess_ms);
        image_infer_ms.push_back(r.infer_ms);
        postprocess_ms.push_back(r.postprocess_ms);
        image_total_ms.push_back(total_ms);
        ++processed;

        LOG_TRACE("[image %zu] pre=%.1f infer=%.1f post=%.1f ms",
                  processed, r.preprocess_ms, r.infer_ms, r.postprocess_ms);
        LOG_INFO("%s -> %s (%.2f%%)",
                 path.filename().c_str(), r.label.c_str(), r.confidence * 100.0f);
    };

    if (std::filesystem::is_directory(cfg.input)) {
        for (const auto &entry : std::filesystem::directory_iterator(cfg.input)) {
            if (hailo_utils::is_image_file(entry.path().string()))
                run_one(entry.path());
        }
    } else {
        run_one(cfg.input);
    }

    const double total_pipeline_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t_total_start).count();

    // ──────────────────────────────────────────────────────────────────────
    // SUMMARY
    // ──────────────────────────────────────────────────────────────────────
    LOG_INFO("==================== PERFORMANCE SUMMARY ====================");
    LOG_INFO("model load: %.2f ms", model_load_ms);
    LOG_INFO("--- TEXT ENCODER (%zu prompts) ---", cfg.prompts.size());
    log_stats("  text_infer (per prompt)", text_stats.per_prompt_ms);
    LOG_INFO("  text pipeline total (tokenize+proj+infer+post): %.2f ms",
             text_stats.total_pipeline_ms);
    LOG_INFO("--- IMAGE ENCODER (%zu images) ---", processed);
    log_stats("  preprocess (cv resize/cvtColor)", preprocess_ms);
    log_stats("  image_infer (HailoInfer)        ", image_infer_ms);
    log_stats("  postprocess (norm/dot/softmax)  ", postprocess_ms);
    log_stats("  image total (pre+infer+post)    ", image_total_ms);
    LOG_INFO("--- WHOLE PIPELINE ---");
    LOG_INFO("  total wall time: %.2f ms (%.2f s)",
             total_pipeline_ms, total_pipeline_ms / 1000.0);
    if (processed > 0) {
        LOG_INFO("  overall throughput: %.2f images/s",
                 1000.0 * processed / total_pipeline_ms);
    }
    LOG_INFO("=============================================================");

    LOG_INFO("zero_shot_classification done");
    return HAILO_SUCCESS;
}
catch (const std::exception &e) {
    LOG_ERROR("exception: %s", e.what());
    return HAILO_INTERNAL_FAILURE;
}
