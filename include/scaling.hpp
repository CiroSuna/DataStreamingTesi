#pragma once

#include <vector>
#include <chrono>
#include <zmq.hpp>

struct QueueState {
    double lambda {0.0};
    double W_ema {0.0};
    double mu_ema {0.0};
    double sum_W {0.0};
    double sum_mu {0.0};
    int warmup_count {0};
    int above_threshold_count {0};
    int below_threshold_count {0};
    int above_W_target_count {0};
    int below_W_target_count {0};
    int K {5};
    int threads {0};
    std::chrono::steady_clock::time_point last_scale_time {};
    double L_estimated {0.0};
    bool pending_thread_update {false};
    std::vector<double> worker_latencys {};
    std::vector<double> tmp_percentile {};
};

// Forward-declare update_type and update_ms from dataTypes.hpp
#include "dataTypes.hpp"

bool thread_update(update_type type, QueueState& qs, const char* worker_topic, int inc_value, zmq::socket_t& update_socket, const int max_threads);
bool realistic_latency_check(QueueState& qs, double W_max);
bool check_update_condition(
    QueueState& qs,
    const char* worker_topic,
    zmq::socket_t& orchestrator,
    bool scaleup_cond,
    bool scaledown_cond,
    int scaleup_delta,
    int scaledown_delta,
    int& above_counter,
    int& below_counter,
    const int max_threads);
