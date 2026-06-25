#include "config.hpp"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

Config load_config(const std::string& path, const std::string& mode) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Cannot load config file '" + path + "': " + e.what());
    }

    if (!root[mode])
        throw std::runtime_error("Mode '" + mode + "' not found in config file '" + path + "'");
    if (!root["global"])
        throw std::runtime_error("Section 'global' not found in config file '" + path + "'");

    const YAML::Node m = root[mode];
    const YAML::Node g = root["global"];

    auto get_d = [&](const YAML::Node& n, const std::string& key) -> double {
        if (!n[key]) throw std::runtime_error("Missing key '" + key + "' in config");
        return n[key].as<double>();
    };
    auto get_i = [&](const YAML::Node& n, const std::string& key) -> int {
        if (!n[key]) throw std::runtime_error("Missing key '" + key + "' in config");
        return n[key].as<int>();
    };

    Config cfg;
    cfg.base_rate_ms      = get_d(m, "base_rate_ms");
    cfg.amplitude_ms      = get_d(m, "amplitude_ms");
    cfg.period_s          = get_d(m, "period_s");
    cfg.run_duration_s    = get_d(m, "run_duration_s");
    cfg.W_max_A_p99       = get_d(m, "W_max_A_p99");
    cfg.W_max_A_p50       = get_d(m, "W_max_A_p50");
    cfg.W_max_B_p99       = get_d(m, "W_max_B_p99");
    cfg.W_max_B_p50       = get_d(m, "W_max_B_p50");

    cfg.alpha             = get_d(g, "alpha");
    cfg.p_target          = get_d(g, "p_target");
    cfg.max_threads       = get_i(g, "max_threads");
    cfg.pause_time_ms     = get_i(g, "pause_time_ms");
    cfg.percentile_window = get_i(g, "percentile_window");
    cfg.warmup_items      = get_i(g, "warmup_items");

    return cfg;
}
