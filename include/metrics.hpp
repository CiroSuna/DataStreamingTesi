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
    std::string get_metrics();
private:
    Metrics();
    int http_requests_total = 0;
    int worker_A_threads = 0;
    int worker_B_threads = 0;
    LatencyHistogram latency_sender_to_A;
    LatencyHistogram latency_A_to_B;
    LatencyHistogram latency_B_to_sink;
    LatencyHistogram latency_end_to_end;
};