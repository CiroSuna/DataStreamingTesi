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

// All information to model the queue as M/M/c 
struct QueueState {
    double lambda {0.0};
    double W_ema {0.0};
    double mu_ema {0.0};
    double W_threshold {0.0};  // set at end of warmup: W_ema_baseline * W_threshold_multiplier
    double sum_W {0.0};
    double sum_mu {0.0};
    int warmup_count {0};
    int64_t warmup_start_time {0};
    int64_t last_arrival_time {0};
    int threads_A {10};
    int threads_B {10};
};

// Update thread count
void thread_update(update_type type, const char* worker_topic, int inc_value, zmq::socket_t& update_socket) {
    update_ms cmd {type, inc_value};
    update_socket.send(zmq_str(worker_topic), zmq::send_flags::sndmore);
    update_socket.send(zmq::message_t(&cmd, sizeof(update_ms)), zmq::send_flags::none);
    update_socket.send(zmq_str(topics::WORKERB), zmq::send_flags::sndmore);
    update_socket.send(zmq::message_t(&cmd, sizeof(update_ms)), zmq::send_flags::none);
}
// Update EMA estimates and apply scaling policy on each new item latency
void handle_item_latency(const item_latency& lat, QueueState& qs, zmq::socket_t& orchestrator) {
    Metrics::instance().observe_item_latency(lat);

    int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();

    if (qs.warmup_count < WARMUP_ITEMS) {
        if (qs.warmup_count == 0)
            qs.warmup_start_time = now;

        qs.sum_W  += lat.end_to_end;
        qs.sum_mu += lat.sender_to_A;
        qs.warmup_count++;

        if (qs.warmup_count == WARMUP_ITEMS) {
            double elapsed_seconds = (now - qs.warmup_start_time) * 1e-9;
            qs.W_ema  = qs.sum_W  / WARMUP_ITEMS;
            qs.mu_ema = 1.0 / (qs.sum_mu / WARMUP_ITEMS);
            qs.lambda = WARMUP_ITEMS / elapsed_seconds;
            qs.W_threshold = qs.W_ema * W_threshold_multiplier;
            qs.last_arrival_time = now;
            LOG_INFO("main", "Warmup complete: lambda=" + std::to_string(qs.lambda) +
                     " mu=" + std::to_string(qs.mu_ema) +
                     " W=" + std::to_string(qs.W_ema) +
                     " W_threshold=" + std::to_string(qs.W_threshold));
        }
    } else {
        // EMA update
        qs.W_ema  = alpha * lat.end_to_end + (1.0 - alpha) * qs.W_ema;
        qs.mu_ema = alpha * (1.0 / lat.sender_to_A) + (1.0 - alpha) * qs.mu_ema;

        double inter_arrival = (now - qs.last_arrival_time) * 1e-9;
        qs.lambda = alpha * (1.0 / inter_arrival) + (1.0 - alpha) * qs.lambda;
        qs.last_arrival_time = now;

        // Minimum servers required for stability: rho = lambda/(c*mu) < 1
        int c_min = static_cast<int>(std::ceil(qs.lambda / qs.mu_ema));

        if (qs.W_ema > qs.W_threshold) {

            thread_update(update_type::THREAD_INC, topics::WORKERA, 1, orchestrator);
            thread_update(update_type::THREAD_INC, topics::WORKERB, 1, orchestrator);
            // threads_A/B and Metrics updated when worker confirms via router
        }
        else if (qs.W_ema < qs.W_threshold / 2.0 && qs.threads_A > c_min && qs.threads_B > c_min) {
            thread_update(update_type::THREAD_DEC, topics::WORKERA, 1, orchestrator);
            thread_update(update_type::THREAD_DEC, topics::WORKERB, 1, orchestrator);
            // threads_A/B and Metrics updated when worker confirms via router
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
    QueueState qs {};

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
                        if(tag_str == msg_types::THREAD_INC) {
                            int val {std::stoi(std::string{static_cast<char*>(payload.data()), payload.size()})};
                            Metrics::instance().inc_worker_A_threads(val);
                            qs.threads_A += val;
                        }
                        else if (tag_str == msg_types::THREAD_DEC) {
                            int val {std::stoi(std::string{static_cast<char*>(payload.data()), payload.size()})};
                            Metrics::instance().inc_worker_A_threads(-val);
                            qs.threads_A -= val;
                        }
                        break;
                    case ProcessId::WORKERB:
                        if(tag_str == msg_types::THREAD_INC) {
                            int val {std::stoi(std::string{static_cast<char*>(payload.data()), payload.size()})};
                            Metrics::instance().inc_worker_B_threads(val);
                            qs.threads_B += val;
                        }
                        else if (tag_str == msg_types::THREAD_DEC) {
                            int val {std::stoi(std::string{static_cast<char*>(payload.data()), payload.size()})};
                            Metrics::instance().inc_worker_B_threads(-val);
                            qs.threads_B -= val;
                        }
                        break;
                    case ProcessId::SENDER:
                        if (tag_str == msg_types::LAMBDA_UPDATE) {
                            std::string value {static_cast<char*>(payload.data()), payload.size()};
                            int new_lambda {std::stoi(value)};

                        }
                        break;
                    case ProcessId::SINK: {
                        if (tag_str == msg_types::ITEM_LATENCY) {
                            item_latency lat {};
                            memcpy(&lat, payload.data(), sizeof(item_latency));
                            handle_item_latency(lat, qs, orchestrator);
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
