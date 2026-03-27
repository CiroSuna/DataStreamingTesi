#pragma once
#include <string>
#include <atomic>
#include <vector>

struct BatchDurationHistogram {
    std::vector<double> bounds;       // bucket upper limits in seconds
    std::vector<long long> bucket_counts; // cumulative counts per bucket (+1 for +Inf)
    double sum{0.0};
    long long count{0};

    BatchDurationHistogram();
    void observe(double seconds);
    std::string serialize() const;
};

class Metrics {
public:
    static Metrics& instance();
    void inc_worker_A_threads();
    void inc_worker_B_threads();
    void observe_batch_duration(double seconds);
    std::string get_metrics();
private:
    Metrics();
    int http_requests_total = 0;
    int worker_A_threads = 0; // Gauge
    int worker_B_threads = 0; // Gauge
    BatchDurationHistogram batch_duration_histogram;
};