#pragma once

#include <CLI/CLI.hpp>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <string>
#include <vector>

namespace hailo_utils {

struct BaseConfig {
    std::string task;
    std::string input;
    std::string output;
};

struct Config : BaseConfig {
    std::string net;
    float       threshold = 0.30f;
};

struct ZeroShotConfig : BaseConfig {
    std::string text_encoder;
    std::string image_encoder;
    std::string text_projection;
    std::string embedding_npy;
    std::string bpe_vocab;
    float       threshold = 0.30f;
    std::vector<std::string> prompts;
};

inline void load_base_config(BaseConfig &cfg, const YAML::Node &root) {
    if (root["task"])   cfg.task   = root["task"].as<std::string>();
    if (root["input"])  cfg.input  = root["input"].as<std::string>();
    if (root["output"]) cfg.output = root["output"].as<std::string>();
}

inline void add_base_options(CLI::App &app, BaseConfig &cfg) {
    app.add_option("--task",   cfg.task,   "Task name");
    app.add_option("--input",  cfg.input,  "Input image or directory");
    app.add_option("--output", cfg.output, "Output directory");
}

// path 끝에 / 없으면 붙여줌
static inline std::string join_path(const std::string &base, const std::string &file) {
    if (base.empty()) return file;
    return (base.back() == '/') ? base + file : base + "/" + file;
}

inline Config load_config(const std::string &yaml_path) {
    Config cfg;
    if (!std::filesystem::exists(yaml_path)) return cfg;

    YAML::Node root = YAML::LoadFile(yaml_path);
    load_base_config(cfg, root);

    YAML::Node node = root["classification"];
    if (!node) return cfg;
    if (node["net"])       cfg.net       = node["net"].as<std::string>();
    if (node["threshold"]) cfg.threshold = node["threshold"].as<float>();
    return cfg;
}

inline Config parse_config(int argc, char **argv, const std::string &yaml_path = "config.yaml") {
    Config cfg = load_config(yaml_path);

    CLI::App app{"Hailo Inference"};
    add_base_options(app, cfg);
    app.add_option("--net",       cfg.net,       "HEF model path");
    app.add_option("--threshold", cfg.threshold, "Confidence threshold");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit(app.exit(e));
    }

    return cfg;
}

inline ZeroShotConfig load_zeroshot_config(const std::string &yaml_path) {
    ZeroShotConfig cfg;
    if (!std::filesystem::exists(yaml_path)) return cfg;

    YAML::Node root = YAML::LoadFile(yaml_path);
    load_base_config(cfg, root);

    YAML::Node node = root["zero_shot_classification"];
    if (!node) return cfg;

    std::string enc_path = node["encoder_path"] ? node["encoder_path"].as<std::string>() : "";
    std::string npy_path = node["npy_path"]      ? node["npy_path"].as<std::string>()     : "";

    if (node["text_encoder"])    cfg.text_encoder    = join_path(enc_path, node["text_encoder"].as<std::string>());
    if (node["image_encoder"])   cfg.image_encoder   = join_path(enc_path, node["image_encoder"].as<std::string>());
    if (node["text_projection"]) cfg.text_projection = join_path(npy_path, node["text_projection"].as<std::string>());
    if (node["embedding_npy"])   cfg.embedding_npy   = join_path(npy_path, node["embedding_npy"].as<std::string>());
    if (node["bpe_vocab"])       cfg.bpe_vocab       = node["bpe_vocab"].as<std::string>();
    if (node["threshold"])       cfg.threshold       = node["threshold"].as<float>();
    if (node["prompts_file"]) {
        std::string pf = node["prompts_file"].as<std::string>();
        if (std::filesystem::exists(pf)) {
            YAML::Node pnode = YAML::LoadFile(pf);
            if (pnode["prompts"] && pnode["prompts"].IsSequence())
                for (const auto &p : pnode["prompts"])
                    cfg.prompts.push_back(p.as<std::string>());
        }
    }
    if (node["prompts"] && node["prompts"].IsSequence()) {
        for (const auto &p : node["prompts"])
            cfg.prompts.push_back(p.as<std::string>());
    }
    return cfg;
}

inline ZeroShotConfig parse_zeroshot_config(int argc, char **argv,
                                            const std::string &yaml_path = "config.yaml") {
    ZeroShotConfig cfg = load_zeroshot_config(yaml_path);

    CLI::App app{"Hailo Zero-Shot Classification"};
    add_base_options(app, cfg);
    app.add_option("--text-encoder",    cfg.text_encoder,    "Text encoder HEF path");
    app.add_option("--image-encoder",   cfg.image_encoder,   "Image encoder HEF path");
    app.add_option("--text-projection", cfg.text_projection, "Path to text_projection.npy");
    app.add_option("--embedding-npy",   cfg.embedding_npy,   "Path to token-embedding .npy");
    app.add_option("--bpe-vocab",       cfg.bpe_vocab,       "Path to BPE vocab file");
    app.add_option("--threshold",       cfg.threshold,       "Confidence threshold");
    app.add_option("--prompt",          cfg.prompts,         "Prompt text (repeatable)")->take_all();

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit(app.exit(e));
    }

    return cfg;
}

struct ObjDetConfig : BaseConfig {
    std::string net;
    float       threshold  = 0.50f;
    size_t      batch_size = 1;
};

inline ObjDetConfig load_objdet_config(const std::string &yaml_path) {
    ObjDetConfig cfg;
    if (!std::filesystem::exists(yaml_path)) return cfg;

    YAML::Node root = YAML::LoadFile(yaml_path);
    load_base_config(cfg, root);

    YAML::Node node = root["object_detection"];
    if (!node) return cfg;
    if (node["net"])        cfg.net        = node["net"].as<std::string>();
    if (node["threshold"])  cfg.threshold  = node["threshold"].as<float>();
    if (node["batch_size"]) cfg.batch_size = node["batch_size"].as<size_t>();
    return cfg;
}

inline ObjDetConfig parse_objdet_config(int argc, char **argv,
                                        const std::string &yaml_path = "config.yaml") {
    ObjDetConfig cfg = load_objdet_config(yaml_path);

    CLI::App app{"Hailo Object Detection"};
    add_base_options(app, cfg);
    app.add_option("--net",        cfg.net,        "HEF model path");
    app.add_option("--threshold",  cfg.threshold,  "Score threshold");
    app.add_option("--batch-size", cfg.batch_size, "Batch size");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit(app.exit(e));
    }

    return cfg;
}

struct SahiObjDetConfig : BaseConfig {
    std::string net;
    float       threshold          = 0.50f;
    float       nmm_iou_threshold  = 0.50f;
    float       overlap_height     = 0.20f;
    float       overlap_width      = 0.20f;
};

inline SahiObjDetConfig load_sahi_config(const std::string &yaml_path) {
    SahiObjDetConfig cfg;
    if (!std::filesystem::exists(yaml_path)) return cfg;

    YAML::Node root = YAML::LoadFile(yaml_path);
    load_base_config(cfg, root);

    YAML::Node node = root["sahi_object_detection"];
    if (!node) return cfg;
    if (node["net"])               cfg.net               = node["net"].as<std::string>();
    if (node["threshold"])         cfg.threshold         = node["threshold"].as<float>();
    if (node["nmm_iou_threshold"]) cfg.nmm_iou_threshold = node["nmm_iou_threshold"].as<float>();
    if (node["overlap_height"])    cfg.overlap_height    = node["overlap_height"].as<float>();
    if (node["overlap_width"])     cfg.overlap_width     = node["overlap_width"].as<float>();
    return cfg;
}

inline SahiObjDetConfig parse_sahi_config(int argc, char **argv,
                                          const std::string &yaml_path = "config.yaml") {
    SahiObjDetConfig cfg = load_sahi_config(yaml_path);

    CLI::App app{"Hailo SAHI Object Detection"};
    add_base_options(app, cfg);
    app.add_option("--net",              cfg.net,              "HEF model path");
    app.add_option("--threshold",        cfg.threshold,        "Score threshold");
    app.add_option("--nmm-iou",          cfg.nmm_iou_threshold,"NMM IoU threshold");
    app.add_option("--overlap-height",   cfg.overlap_height,   "Slice overlap ratio (height)");
    app.add_option("--overlap-width",    cfg.overlap_width,    "Slice overlap ratio (width)");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit(app.exit(e));
    }

    return cfg;
}

} // namespace hailo_utils
