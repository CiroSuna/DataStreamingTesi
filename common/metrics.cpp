#include "metrics.hpp"
#include <sstream>

BatchDurationHistogram::BatchDurationHistogram() : 
    bounds{0.5, 1.0, 1.5, 2.0, 3.0, 5.0},
    bucket_counts(bounds.size() + 1, 0)  // +1 for +Inf
{}

void BatchDurationHistogram::observe(double seconds) {
    for (size_t i = 0; i < bounds.size(); ++i) {
        if (seconds <= bounds[i])
            bucket_counts[i]++;
    }
    bucket_counts[bounds.size()]++;  // +Inf always incremented
    sum += seconds;
    count++;
}

std::string BatchDurationHistogram::serialize() const {
    std::ostringstream oss;
    oss << "# HELP batch_duration_seconds End-to-end time to process a complete batch through the pipeline\n";
    oss << "# TYPE batch_duration_seconds histogram\n";
    for (size_t i = 0; i < bounds.size(); ++i)
        oss << "batch_duration_seconds_bucket{le=\"" << bounds[i] << "\"} " << bucket_counts[i] << "\n";
    oss << "batch_duration_seconds_bucket{le=\"+Inf\"} " << bucket_counts[bounds.size()] << "\n";
    oss << "batch_duration_seconds_sum " << sum << "\n";
    oss << "batch_duration_seconds_count " << count << "\n";
    return oss.str();
}

Metrics::Metrics() {}

Metrics& Metrics::instance() {
    static Metrics m;
    return m;
}

void Metrics::inc_worker_A_threads(int inc_value = 1) {
    worker_A_threads += inc_value;
}

void Metrics::inc_worker_B_threads(int inc_value = 1) {
    worker_B_threads += inc_value;
}

void Metrics::observe_batch_duration(double seconds) {
    batch_duration_histogram.observe(seconds);
}

std::string Metrics::get_metrics() {
    std::ostringstream oss;
    oss << "# HELP worker_A_threads Active workerA threads\n";
    oss << "# TYPE worker_A_threads gauge\n";
    oss << "worker_A_threads " << worker_A_threads << "\n";
    oss << "# HELP worker_B_threads Active workerB threads\n";
    oss << "# TYPE worker_B_threads gauge\n";
    oss << "worker_B_threads " << worker_B_threads << "\n";
    oss << batch_duration_histogram.serialize();
    return oss.str();
}


