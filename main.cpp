#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <zmq.hpp>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include "dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include "metrics.hpp"
#include "scaling.hpp"
#include "httplib.h"


constexpr int PIPE_LENGTH {4};
constexpr int WARMUP_ITEMS {20};
constexpr double alpha {0.1};
constexpr double p_target {0.7};          // target utilization per worker
constexpr int max_threads {80};
// WorkerA service_time=20ms -> c_min=6, W_min=0.020s
constexpr double W_max_A_p99 {0.06};      // 3x service time A
constexpr double W_max_A_p50 {0.035};     // 1.75x service time A
// WorkerB service_time=50ms -> c_min=15, W_min=0.050s
constexpr double W_max_B_p99 {0.15};      // 3x service time B
constexpr double W_max_B_p50 {0.08};      // 1.6x service time B
constexpr size_t PERCENTILE_WINDOW {200}; // ~1s of data at 200 items/s
constexpr int pause_time {500};

// Flag to tell when to shutdown pipe
std::atomic<bool> end_pipe {false};

void rate_updater(std::atomic<int>& send_rate) {
    std::string new_rate;
    int rate {};
    while (true) {
        std::cout << "\nInsert a new send rate (in ms): \n";
        std::cin >> new_rate; 
        std::cout << '\n';
        try {
            rate = std::stoi(new_rate);
        }
        catch (std::invalid_argument& e) {
            std::cout << "Invalid argument " << e.what();
            continue;
        }
        catch (std::out_of_range& e) {
            std::cout << "Out of range number" << e.what();
            continue;
        }
        send_rate.store(rate); 
        std::cout << send_rate.load();
    }
}


void process_worker_latency_common(
    const item_latency& lat,
    QueueState& qs,
    const char* worker,
    double observed_latency,
    double service_time,
    LatencyHistogram& hist,
    double W_max_p99,
    double W_max_p50,
    zmq::socket_t& orchestrator)
{
    // warmup + metrics
    if (qs.warmup_count < WARMUP_ITEMS) {
        qs.sum_W += observed_latency;
        qs.sum_mu += 1.0 / service_time;
        qs.worker_latencys.push_back(observed_latency);

        qs.warmup_count++;
        if (qs.warmup_count == WARMUP_ITEMS) {
            qs.W_ema = qs.sum_W / WARMUP_ITEMS;
            qs.mu_ema = qs.sum_mu / WARMUP_ITEMS;
            Metrics::instance().set_queue_state(qs.lambda, qs.mu_ema, qs.W_ema, 0.0, worker);
            LOG_INFO("main", "Warmup complete: lambda=" + std::to_string(qs.lambda) +
                     " mu=" + std::to_string(qs.mu_ema) +
                     " W=" + std::to_string(qs.W_ema));
        }
        return;
    }

    // EMA updates
    qs.W_ema = alpha * observed_latency + (1.0 - alpha) * qs.W_ema;
    qs.mu_ema = alpha * (1.0 / service_time) + (1.0 - alpha) * qs.mu_ema;

    
    // sliding window population
    qs.worker_latencys.push_back(observed_latency);
    if (qs.worker_latencys.size() > PERCENTILE_WINDOW)
        qs.worker_latencys.erase(qs.worker_latencys.begin());
     
    auto now = std::chrono::steady_clock::now();
    // Wait 300ms before re-evaluating scaling
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - qs.last_scale_time).count() < pause_time) {
        return;
    }
    if (qs.pending_thread_update) {
        return;
    }
    // compute p99 from window
    double p99_window = 0.0;
    double p50_window = 0.0;
    std::vector<double> tmp;
    tmp = qs.worker_latencys; // copies; reuses capacity if possible

    if (!tmp.empty()) {
        size_t idx = static_cast<size_t>(std::ceil(0.99 * tmp.size()));
        if (idx == 0) idx = 1;
        idx = idx - 1; 
        std::nth_element(tmp.begin(), tmp.begin() + idx, tmp.end());
        p99_window = tmp[idx];
    }
    tmp = qs.worker_latencys;

    if (!tmp.empty()) {
        size_t idx = static_cast<size_t>(std::ceil(0.50 * tmp.size()));
        if (idx == 0) idx = 1;
        idx = idx - 1;
        std::nth_element(tmp.begin(), tmp.begin() + idx, tmp.end());
        p50_window = tmp[idx];
    }


    qs.L_estimated = static_cast<int>(std::ceil(qs.lambda * (qs.W_ema - 1.0 / qs.mu_ema)));
    Metrics::instance().set_queue_state(qs.lambda, qs.mu_ema, qs.W_ema, qs.L_estimated, worker);

    if (qs.lambda <= 0.0) return;
    int c_min = static_cast<int>(std::ceil(qs.lambda / (qs.mu_ema * p_target)));

    // c_min based scale decision
    
    check_update_condition(qs, worker, orchestrator,
                           qs.threads < c_min, qs.threads > c_min,
                           c_min - qs.threads, 5, max_threads);
                           
    if (!realistic_latency_check(qs, W_max_p99)) return;

    // p50 based scale decision
    check_update_condition(qs, worker, orchestrator,
                           p50_window > W_max_p50, p50_window < 0.8 * W_max_p50,
                           1, 2, max_threads);
    
    // p99 based scale decision
    check_update_condition(qs, worker, orchestrator,
                           p99_window > W_max_p99, p99_window < 0.8 * W_max_p99,
                           1, 5, max_threads);
}


// Nuova handle_item_latency che delega
void handle_item_latency(const item_latency& lat, QueueState& qs, const char* worker, zmq::socket_t& orchestrator) {
    Metrics::instance().observe_item_latency(lat);

    if (worker == topics::WORKERA) {
        process_worker_latency_common(
            lat, qs, worker, lat.sender_to_A, lat.service_time_A,
            Metrics::instance().latency_A_to_B, W_max_A_p99, W_max_A_p50, orchestrator
        );
    } else {
        process_worker_latency_common(
            lat, qs, worker, lat.A_to_B, lat.service_time_B,
            Metrics::instance().latency_B_to_sink, W_max_B_p99, W_max_B_p50, orchestrator
        );
    }
}

void thread_update_recv(const std::string& tag_str, const char* worker, const zmq::message_t& payload, QueueState& qs) {
    int val {std::stoi(std::string{static_cast<const char*>(payload.data()), payload.size()})};
    int sign {(tag_str == msg_types::THREAD_INC) ? 1 : -1};
    Metrics::instance().inc_worker_threads(sign * val, worker);
    qs.threads += sign * val;
    qs.pending_thread_update = false;
}

// Exiting on error function
[[noreturn]] void fatal(const std::string& msg) {
    std::cerr << msg << '\n';
    kill(0, SIGTERM);

    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(-1, nullptr, 0);

    exit(EXIT_FAILURE);
}

pid_t process_starter(const std::string& process_name){
    pid_t new_proc {fork()};
    if (new_proc == -1) {
        fatal("Errore fork " + process_name + ": " + strerror(errno));
    }
    else if (new_proc == 0) {
        std::string path {"./" + process_name + "/" + process_name};
        execl(path.c_str(), process_name.c_str(), nullptr);
        perror(("Errore execl " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
    
    return new_proc;
}

void on_sigchld(int) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        std::cerr << "Child " << pid << " ended execution shutting down\n";
        kill(0, SIGTERM);
    }
    
}

void on_sigint(int) {
    kill(0, SIGTERM);
    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(-1, nullptr, 0);
}

// Wait for expected READY messages from child processes, reply GO to each
void wait_for_ready(zmq::socket_t& sync_socket, int expected) {
    int ready_count {0};
    while (ready_count < expected) {
        zmq::message_t msg;
        try {
            auto res = sync_socket.recv(msg, zmq::recv_flags::none);
            if (!res.has_value()) {
                std::cerr << "Timeout waiting for READY messages. Got " << ready_count << "/" << expected << ", retrying...\n";
                continue;
            }
            std::string r {static_cast<char*>(msg.data()), msg.size()};
            LOG_DEBUG("main", "Received: " + r + " (" + std::to_string(ready_count + 1) + "/" + std::to_string(expected) + ")");
            if (r == messages::READY) {
                ++ready_count;
                LOG_INFO("main", "Worker ready (" + std::to_string(ready_count) + "/" + std::to_string(expected) + ")");
                sync_socket.send(zmq_str(messages::GO), zmq::send_flags::none);
            }
        }
        catch (const zmq::error_t& e) {
            fatal("Timeout waiting for READY messages. Got " + std::to_string(ready_count) + "/" + std::to_string(expected));
        }
    }
}




int main(void){

    // Create file for latencys
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    std::string csv_path = std::string(LOG_DIR) + "/run_" + ts_buf + ".csv";
    std::ofstream csv_file(csv_path);
    // scrivi header
    csv_file << "send_time_ns,exit_time_ns,sender_to_A_s,service_time_A_s,"
                "A_to_B_s,service_time_B_s,B_to_sink_s,end_to_end_s,threads_A,threads_B,lambda,"
                "queue_len_A,queue_len_B\n";
    // Variables for calculating system stability and little formula
    QueueState qsA {};
    QueueState qsB {};
    std::atomic<int> send_rate {15}; // rate at which the sender sends data in ms
    int old_rate {send_rate.load()};
    
    Logger::instance().init(std::string(LOG_DIR) + "/main.log");
    signal(SIGINT, on_sigint);
    signal(SIGCHLD, on_sigchld);

    // Cleanup for old ipc files in tmp
    cleanup_ipc_path(ipc_paths::orchestrator());
    cleanup_ipc_path(ipc_paths::sync_socket_path());
    cleanup_ipc_path(ipc_paths::sender_to_workerA());
    cleanup_ipc_path(ipc_paths::workerA_to_workerB());
    cleanup_ipc_path(ipc_paths::workerB_to_sink());

    httplib::Server metrics_exposer;
    metrics_exposer.Get("/metrics", [](const httplib::Request&, httplib::Response& res){
        res.set_content(Metrics::instance().get_metrics(), "text/plain");
    });

    std::thread metrics_thread([&metrics_exposer](){
        metrics_exposer.listen("0.0.0.0", 8080);
    });


   
    // Starting contex
    zmq::context_t ctx {};
    zmq::socket_t orchestrator {ctx, zmq::socket_type::pub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::rep};
    zmq::socket_t router {ctx, zmq::socket_type::router};

    try {
        orchestrator.bind(ipc_paths::orchestrator());
        sync_socket.bind(ipc_paths::sync_socket_path());
        router.bind(ipc_paths::router_path());
        sync_socket.set(zmq::sockopt::rcvtimeo, 5000);
    }
    catch (const zmq::error_t& e) {
        fatal(std::string("orchestrator: ") + e.what());
    }

    pid_t p_child[PIPE_LENGTH] {
        process_starter("workerA"),
        process_starter("workerB"),
        process_starter("sink"),
        process_starter("sender")
    };

    // Sync 
    LOG_INFO("main", "Starting to wait for READY messages...");
    wait_for_ready(sync_socket, PIPE_LENGTH);
    LOG_INFO("main", "All processes synced and in execution");
    
    // Start thread to check manual send rate update in cin
    std::thread rate_updater_thread(rate_updater, std::ref(send_rate));


    zmq::pollitem_t items[2] {
        {sync_socket, 0, ZMQ_POLLIN, 0},
        {router, 0, ZMQ_POLLIN, 0}
    };
    
    try {
        // Poll loop
        while (true) {
            zmq::poll(items, 2, std::chrono::milliseconds(100));
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t msg_final;
                (void)sync_socket.recv(msg_final, zmq::recv_flags::none);

                std::string final_str {static_cast<char*>(msg_final.data()), msg_final.size()};

                if (final_str != messages::END) {
                    fatal("Error: wrong message received, expected: END, got: " + final_str);
                }

                LOG_INFO("main", "sink: " + final_str + " dato arrivato al sink");
                sync_socket.send(zmq_str(messages::OK), zmq::send_flags::none);  
                break;
            }

            if (items[1].revents & ZMQ_POLLIN) {
                zmq::message_t identity;
                zmq::message_t tag;
                zmq::message_t payload;
                auto status {router.recv(identity, zmq::recv_flags::dontwait)};
                if (!status.has_value())
                    continue;
                (void)router.recv(tag, zmq::recv_flags::dontwait);
                (void)router.recv(payload, zmq::recv_flags::dontwait);

                ProcessId sender_id {parse_process_id(
                    std::string{static_cast<char*>(identity.data()), identity.size()}
                )};
                std::string tag_str {static_cast<char*>(tag.data()), tag.size()};

                // Handle request from process node
                switch (sender_id) {
                    case ProcessId::WORKERA:
                        if (tag_str == msg_types::THREAD_INC || tag_str == msg_types::THREAD_DEC) {
                            thread_update_recv(tag_str, topics::WORKERA, payload, qsA);
                        }
                        break;
                    case ProcessId::WORKERB:
                        if (tag_str == msg_types::THREAD_INC || tag_str == msg_types::THREAD_DEC) {
                            thread_update_recv(tag_str, topics::WORKERB, payload, qsB);
                        }
                        break;
                    case ProcessId::SENDER:
                        if (tag_str == msg_types::LAMBDA_UPDATE) {
                            std::string payload_str {static_cast<char*>(payload.data()), payload.size()};
                            int64_t int_arr_ns = std::stoll(payload_str);
                            double new_lambda = 1.0 / (int_arr_ns * 1e-9);
                            qsA.lambda = alpha * new_lambda + (1.0 - alpha) * qsA.lambda;
                            // Check if mu is not 0 (warmup is over)
                            double throughput_A = (qsA.mu_ema > 0.0 && qsA.threads > 0)
                                ? qsA.mu_ema * qsA.threads
                                : new_lambda;
                            
                            // Choose to use lambda_a or throughput, using min because throuput_A can be theorical and be higher than lambda 
                            double lambda_B_instant = std::min(qsA.lambda, throughput_A); 
                            qsB.lambda = alpha * lambda_B_instant + (1.0 - alpha) * qsB.lambda;
                        }
                        break;
                    case ProcessId::SINK: {
                        if (tag_str == msg_types::ITEM_LATENCY) {
                            item_latency lat {};
                            memcpy(&lat, payload.data(), sizeof(item_latency));
                            handle_item_latency(lat, qsA, topics::WORKERA, orchestrator);
                            handle_item_latency(lat, qsB, topics::WORKERB, orchestrator);

                            csv_file << lat.send_time << ","
                                << (lat.send_time + static_cast<int64_t>(lat.end_to_end * 1e9)) << ","
                                << lat.sender_to_A << "," << lat.service_time_A << ","
                                << lat.A_to_B << "," << lat.service_time_B << ","
                                << lat.B_to_sink << "," << lat.end_to_end << ","
                                << qsA.threads << "," << qsB.threads << ","
                                << qsA.lambda << ","
                                << qsA.L_estimated << "," << qsB.L_estimated << '\n';
                        }
                        break;
                    }
                    case ProcessId::UNKNOWN:
                        LOG_DEBUG("main", "Unknown identity on router socket");
                        break;
                }
            }
            int curr_rate = send_rate.load(); 
            if (curr_rate != old_rate) {
                old_rate = curr_rate;
                orchestrator.send(zmq_str(topics::SENDER), zmq::send_flags::sndmore);
                orchestrator.send(zmq_str(msg_types::RATE_UPDATE), zmq::send_flags::sndmore);
                orchestrator.send(zmq::buffer(std::to_string(curr_rate)), zmq::send_flags::none);
            }
        } 
        
    }
    catch (const zmq::error_t& e) {
        fatal(std::string("Error waiting for sink END message: ") + e.what());
    }

    orchestrator.send(zmq_str(topics::GLOBAL), zmq::send_flags::sndmore);
    orchestrator.send(zmq_str(messages::SHUTDOWN), zmq::send_flags::none);
    
    // Wait for all children to finish before closing sockets
    // IMPORTANT waitpid could return -1 with ECHILD errno if sigchild was previusly handeld, here is voluntarily ignored
    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(p_child[i], nullptr, 0);

    // Shutdown get handler thread
    rate_updater_thread.join();
    metrics_exposer.stop();
    metrics_thread.join();
    
    return 0;
}
