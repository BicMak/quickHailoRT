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

} // namespace hailo_utils
