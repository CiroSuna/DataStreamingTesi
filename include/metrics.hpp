#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "dataTypes.hpp"
#include <unordered_map>

struct LatencyHistogram {
    private:
        std::string name;
        std::string help;
        std::vector<double> bounds;
        std::vector<long long> bucket_counts;
        double sum{0.0};
        long long count{0};
    public:
        LatencyHistogram(const std::string& name, const std::string& help);
        void observe(double seconds);
        std::string serialize() const;
        double get_percentile(double p);
};

struct WorkerState {
    double lambda {0.0};
    int threads {0};
    double mu {0.0};
    double W {0.0};
    int L {0};
    std::string label;
};

class Metrics {
public:
    static Metrics& instance();
    void inc_worker_threads(int inc_value, const char* worker);
    void observe_item_latency(const item_latency& lat);
    void set_queue_state(double lambda, double mu, double W, int L, const char* worker);
    void set_sender_bp_stall(double fraction);
    std::string get_metrics();
private:
    Metrics();
    std::unordered_map<std::string, WorkerState> workers_info;
    std::unordered_map<std::string, LatencyHistogram> latencies;
    double sender_bp_stall_fraction {0.0};
    std::mutex mutex;
};
