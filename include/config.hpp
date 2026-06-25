#pragma once
#include <string>

struct Config {
    // Sender / load parameters (per mode)
    double base_rate_ms;
    double amplitude_ms;
    double period_s;
    double run_duration_s;
    // Scaling latency thresholds (per mode)
    double W_max_A_p99;
    double W_max_A_p50;
    double W_max_B_p99;
    double W_max_B_p50;
    // Global algorithm parameters
    double alpha;
    double p_target;
    int max_threads;
    int pause_time_ms;
    int percentile_window;
    int warmup_items;
};

// Load the section [mode] from a YAML config file.
// Throws std::runtime_error on missing file, missing section, or missing key.
Config load_config(const std::string& path, const std::string& mode);
