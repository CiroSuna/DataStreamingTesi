#include "../include/scaling.hpp"
#include "../include/dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include <algorithm>

// Note: returns true if an update was sent
bool thread_update(update_type type, QueueState& qs, const char* worker_topic, int inc_value, zmq::socket_t& update_socket, const int max_threads) {
    if (qs.pending_thread_update) {
        return false;
    }
    int delta = inc_value;
    if (type == update_type::THREAD_INC) {
        int room = max_threads - qs.threads;
        if (room <= 0) {
            LOG_DEBUG("main", "reached max number of thread");
            return false;
        }
        delta = std::min(delta, room);
    } else {
        int removable = qs.threads - 1;
        if (removable <= 0) {
            return false;
        }
        delta = std::min(delta, removable);
    }

    if (delta <= 0) return false;

    update_ms cmd {type, delta};
    update_socket.send(zmq_str(worker_topic), zmq::send_flags::sndmore);
    update_socket.send(zmq::message_t(&cmd, sizeof(update_ms)), zmq::send_flags::none);
    qs.pending_thread_update = true;
    qs.last_scale_time = std::chrono::steady_clock::now();
    return true;
}

bool realistic_latency_check(QueueState& qs, double W_max) {
    double W_physical_min = 1.0 / qs.mu_ema;
    if (W_max < W_physical_min) {
        LOG_INFO("main", "SLA impossible: W_max = " 
                 + std::to_string(W_max) 
                 + " Service time: " 
                 + std::to_string(W_physical_min));
        return false;
    }
    return true;
}

bool check_update_condition(
    QueueState& qs,
    const char* worker_topic,
    zmq::socket_t& orchestrator,
    bool scaleup_cond,
    bool scaledown_cond,
    int scaleup_delta,
    int scaledown_delta,
    const int max_threads)
{
    if (qs.pending_thread_update) return false;
    update_type type {};
    int delta = 0;

    if (scaleup_cond) {
        type = update_type::THREAD_INC;
        delta = scaleup_delta;
    } else if (scaledown_cond) {
        type = update_type::THREAD_DEC;
        delta = scaledown_delta;
    } else {
        return false;
    }

    return thread_update(type, qs, worker_topic, delta, orchestrator, max_threads);

    return false;
}
