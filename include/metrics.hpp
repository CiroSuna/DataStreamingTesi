#pragma once
#include <string>
#include <atomic>
#include <vector>
#include "dataTypes.hpp"

struct LatencyHistogram {
    std::string name;
    std::string help;
    std::vector<double> bounds;
    std::vector<long long> bucket_counts;
    double sum{0.0};
    long long count{0};

    LatencyHistogram(const std::string& name, const std::string& help);
    void observe(double seconds);
    std::string serialize() const;
};

class Metrics {
public:
    static Metrics& instance();
    void inc_worker_A_threads(int inc_value);
    void inc_worker_B_threads(int inc_value);
    void observe_item_latency(const item_latency& lat);
    void set_queue_state_A(double lambda, double mu, double W);
    void set_queue_state_B(double lambda, double mu, double W);
    std::string get_metrics();
private:
    Metrics();
    int http_requests_total = 0;
    int worker_A_threads = 0;
    int worker_B_threads = 0;
    double qs_lambda_A{0.0};
    double qs_mu_A{0.0};
    double qs_W_A{0.0};
    double qs_lambda_B{0.0};
    double qs_mu_B{0.0};
    double qs_W_B{0.0};
    LatencyHistogram latency_sender_to_A;
    LatencyHistogram latency_A_to_B;
    LatencyHistogram latency_B_to_sink;
    LatencyHistogram latency_end_to_end;
};