#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include "metrics.hpp"
#include "httplib.h"


constexpr int PIPE_LENGTH {4};
constexpr int WARMUP_ITEMS {20};
constexpr double alpha {0.1};
constexpr double W_threshold_multiplier {1.5};  // W_threshold = W_ema_baseline * this

// Flag to tell when to shutdown pipe
std::atomic<bool> end_pipe {false};

void rate_updater() {
    // TODO: create thread to check if stdin has update value for input item rate (in ms)
}
// All information to model the queue as M/M/c 
struct QueueState {
    double lambda {0.0}; // Items/s to arrive
    double W_ema {0.0}; // End-to-end latency for a single worker
    double mu_ema {0.0}; // Items/s to exit
    double W_threshold {0.0};  // set at end of warmup: W_ema_baseline * W_threshold_multiplier, never updated after warmup
    double sum_W {0.0}; // Sum for warmup
    double sum_mu {0.0}; // Sum for warmup
    int warmup_count {0}; // Checks how many items used for warmup
    int above_threshold_count {0};
    int below_threshold_count {0};
    int K {5}; // Number of times W can go below/above its threshold, used to avoid updates made by outliers
    int threads {10}; // Thread present in a worker node
};

// Update thread count
void thread_update(update_type type, const char* worker_topic, int inc_value, zmq::socket_t& update_socket) {
    update_ms cmd {type, inc_value};
    update_socket.send(zmq_str(worker_topic), zmq::send_flags::sndmore);
    update_socket.send(zmq::message_t(&cmd, sizeof(update_ms)), zmq::send_flags::none);
}
// Update EMA estimates and apply scaling policy on each new item latency
void handle_item_latency(const item_latency& lat, QueueState& qs, const char* worker, zmq::socket_t& orchestrator) {
    Metrics::instance().observe_item_latency(lat);

    // Warmup stem, aproximating W, lambda and mu with the first n items
    if (qs.warmup_count < WARMUP_ITEMS) {
        if (worker == topics::WORKERA) {
            qs.sum_W  += lat.sender_to_A;
            qs.sum_mu += 1.0 / lat.service_time_A;
        }
        else {
            qs.sum_W  += lat.A_to_B;
            qs.sum_mu += 1.0 / lat.service_time_B;
        }
        qs.warmup_count++;

        // When warmup finished starts to calculate approximations
        if (qs.warmup_count == WARMUP_ITEMS) {
            qs.W_ema  = qs.sum_W  / WARMUP_ITEMS;
            qs.mu_ema = qs.sum_mu / WARMUP_ITEMS;
            qs.W_threshold = qs.W_ema * W_threshold_multiplier;
            if (worker == topics::WORKERA)
                Metrics::instance().set_queue_state_A(qs.lambda, qs.mu_ema, qs.W_ema);
            else
                Metrics::instance().set_queue_state_B(qs.lambda, qs.mu_ema, qs.W_ema);
            LOG_INFO("main", "Warmup complete: lambda=" + std::to_string(qs.lambda) +
                     " mu=" + std::to_string(qs.mu_ema) +
                     " W=" + std::to_string(qs.W_ema) +
                     " W_threshold=" + std::to_string(qs.W_threshold));
        }
    } else {
        // EMA update
        if (worker == topics::WORKERA) {
            qs.W_ema  = alpha * lat.sender_to_A + (1.0 - alpha) * qs.W_ema;
            qs.mu_ema = alpha * (1.0 / lat.service_time_A) + (1.0 - alpha) * qs.mu_ema;
        } else {
            qs.W_ema  = alpha * lat.A_to_B + (1.0 - alpha) * qs.W_ema;
            qs.mu_ema = alpha * (1.0 / lat.service_time_B) + (1.0 - alpha) * qs.mu_ema;
        }

        if (worker == topics::WORKERA)
            Metrics::instance().set_queue_state_A(qs.lambda, qs.mu_ema, qs.W_ema);
        else
            Metrics::instance().set_queue_state_B(qs.lambda, qs.mu_ema, qs.W_ema);

        // Minimum servers required for stability: rho = lambda/(c*mu) < 1
        int c_min = static_cast<int>(std::ceil(qs.lambda / qs.mu_ema));

        if (qs.W_ema > qs.W_threshold) {
            qs.above_threshold_count++;
            qs.below_threshold_count = 0;
            if (qs.above_threshold_count >= qs.K) {
                thread_update(update_type::THREAD_INC, worker, 1, orchestrator);
                qs.above_threshold_count = 0;
                // threads updated when worker confirms via router
            }
        }
        else if (qs.W_ema < qs.W_threshold / 2.0 && qs.threads > c_min) {
            qs.below_threshold_count++;
            qs.above_threshold_count = 0;
            if (qs.below_threshold_count >= qs.K) {
                thread_update(update_type::THREAD_DEC, worker, 1, orchestrator);
                qs.below_threshold_count = 0;
                // threads updated when worker confirms via router
            }
        }
        else {
            qs.above_threshold_count = 0;
            qs.below_threshold_count = 0;
        }
    }
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

// Wait for "expected" READY messages from child processes, reply GO to each
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

    // Variables for calculating system stability and little formula
    QueueState qsA {};
    QueueState qsB {};

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

    pid_t p_child[PIPE_LENGTH] {
        process_starter("workerA"),
        process_starter("workerB"),
        process_starter("sink"),
        process_starter("sender")
    };

   
    // Starting context, sockets and binding FIRST
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

    // Sync 
    LOG_INFO("main", "Starting to wait for READY messages...");
    wait_for_ready(sync_socket, PIPE_LENGTH);
    LOG_INFO("main", "All processes synced and in execution");

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
                            int val {std::stoi(std::string{static_cast<char*>(payload.data()), payload.size()})};
                            int sign {(tag_str == msg_types::THREAD_INC) ? 1 : -1};
                            Metrics::instance().inc_worker_A_threads(sign * val);
                            qsA.threads += sign * val;
                        }
                        break;
                    case ProcessId::WORKERB:
                        if (tag_str == msg_types::THREAD_INC || tag_str == msg_types::THREAD_DEC) {
                            int val {std::stoi(std::string{static_cast<char*>(payload.data()), payload.size()})};
                            int sign {(tag_str == msg_types::THREAD_INC) ? 1 : -1};
                            Metrics::instance().inc_worker_B_threads(sign * val);
                            qsB.threads += sign * val;
                        }
                        break;
                    case ProcessId::SENDER:
                        if (tag_str == msg_types::LAMBDA_UPDATE) {
                            std::string payload_str {static_cast<char*>(payload.data()), payload.size()};
                            int64_t int_arr_ns = std::stoll(payload_str);
                            double new_lambda = 1.0 / (int_arr_ns * 1e-9);
                            qsA.lambda = alpha * new_lambda + (1.0 - alpha) * qsA.lambda;
                            qsB.lambda = alpha * new_lambda + (1.0 - alpha) * qsB.lambda;
                        }
                        break;
                    case ProcessId::SINK: {
                        if (tag_str == msg_types::ITEM_LATENCY) {
                            item_latency lat {};
                            memcpy(&lat, payload.data(), sizeof(item_latency));
                            handle_item_latency(lat, qsA, topics::WORKERA, orchestrator);
                            handle_item_latency(lat, qsB, topics::WORKERB, orchestrator);
                        }
                        break;
                    }
                    case ProcessId::UNKNOWN:
                        LOG_DEBUG("main", "Unknown identity on router socket");
                        break;
                }
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
    metrics_exposer.stop();
    metrics_thread.join();

    return 0;
}
