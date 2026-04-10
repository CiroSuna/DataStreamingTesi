#include "metrics.hpp"
#include <sstream>

LatencyHistogram::LatencyHistogram(const std::string& n, const std::string& h)
    : name{n}, help{h},
      bounds{0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0},
      bucket_counts(bounds.size() + 1, 0)
{}

void LatencyHistogram::observe(double seconds) {
    for (size_t i = 0; i < bounds.size(); ++i) {
        if (seconds <= bounds[i])
            bucket_counts[i]++;
    }
    bucket_counts[bounds.size()]++;
    sum += seconds;
    count++;
}
// Serialize data fot exposition on /metrics
std::string LatencyHistogram::serialize() const {
    std::ostringstream oss;
    oss << "# HELP " << name << " " << help << "\n";
    oss << "# TYPE " << name << " histogram\n";
    for (size_t i = 0; i < bounds.size(); ++i)
        oss << name << "_bucket{le=\"" << bounds[i] << "\"} " << bucket_counts[i] << "\n";
    oss << name << "_bucket{le=\"+Inf\"} " << bucket_counts[bounds.size()] << "\n";
    oss << name << "_sum " << sum << "\n";
    oss << name << "_count " << count << "\n";
    return oss.str();
}

Metrics::Metrics()
    : latency_sender_to_A{"pipeline_latency_sender_to_workerA_seconds", "Latency from sender to workerA per item"},
      latency_A_to_B{"pipeline_latency_workerA_to_workerB_seconds", "Latency from workerA to workerB per item"},
      latency_B_to_sink{"pipeline_latency_workerB_to_sink_seconds", "Latency from workerB to sink per item"},
      latency_end_to_end{"pipeline_latency_end_to_end_seconds", "End-to-end latency per item through the pipeline"}
{}

Metrics& Metrics::instance() {
    static Metrics m;
    return m;
}

void Metrics::inc_worker_A_threads(int inc_value) {
    worker_A_threads += inc_value;
}

void Metrics::inc_worker_B_threads(int inc_value) {
    worker_B_threads += inc_value;
}

// Update histogram with new latencys
void Metrics::observe_item_latency(const item_latency& lat) {
    latency_sender_to_A.observe(lat.sender_to_A);
    latency_A_to_B.observe(lat.A_to_B);
    latency_B_to_sink.observe(lat.B_to_sink);
    latency_end_to_end.observe(lat.end_to_end);
}

std::string Metrics::get_metrics() {
    std::ostringstream oss;
    oss << "# HELP worker_A_threads Active workerA threads\n";
    oss << "# TYPE worker_A_threads gauge\n";
    oss << "worker_A_threads " << worker_A_threads << "\n";
    oss << "# HELP worker_B_threads Active workerB threads\n";
    oss << "# TYPE worker_B_threads gauge\n";
    oss << "worker_B_threads " << worker_B_threads << "\n";
    oss << latency_sender_to_A.serialize();
    oss << latency_A_to_B.serialize();
    oss << latency_B_to_sink.serialize();
    oss << latency_end_to_end.serialize();
    return oss.str();
}


