#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"
#include "metrics.hpp"
#include "httplib.h"


constexpr int PIPE_LENGTH {4};

// Flag to tell when to shutdown pipe
std::atomic<bool> end_pipe {false};

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
                sync_socket.send(zmq::message_t(messages::GO, 2), zmq::send_flags::none);
            }
        }
        catch (const zmq::error_t& e) {
            fatal("Timeout waiting for READY messages. Got " + std::to_string(ready_count) + "/" + std::to_string(expected));
        }
    }
}




int main(void){
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
                sync_socket.send(zmq::message_t(messages::OK, 2), zmq::send_flags::none);  
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

                switch (sender_id) {
                    case ProcessId::WORKERA:
                        if(tag_str == msg_types::THREAD_INC) {
                            std::string value {static_cast<char*>(payload.data()), payload.size()};
                            Metrics::instance().inc_worker_A_threads(std::stoi(value));
                        }
                        else if (tag_str == msg_types::THREAD_DEC) {
                            std::string value {static_cast<char*>(payload.data()), payload.size()};
                            Metrics::instance().inc_worker_A_threads(-std::stoi(value));
                        }
                        break;
                    case ProcessId::WORKERB:
                        if(tag_str == msg_types::THREAD_INC) {
                            std::string value {static_cast<char*>(payload.data()), payload.size()};
                            Metrics::instance().inc_worker_B_threads(std::stoi(value));
                        }
                        else if (tag_str == msg_types::THREAD_DEC) {
                            std::string value {static_cast<char*>(payload.data()), payload.size()};
                            Metrics::instance().inc_worker_B_threads(-std::stoi(value));
                        }
                        break;
                    case ProcessId::SENDER:
                        break;
                    case ProcessId::SINK: {
                        if (tag_str == msg_types::BATCH_DURATION) {
                            std::string value {static_cast<char*>(payload.data()), payload.size()};

                            update_ms threadpool_resize {update_type::THREAD_INC, 5};
                            // Update worker A thread size
                            orchestrator.send(zmq::message_t {topics::workera_topic()}, zmq::send_flags::sndmore);
                            orchestrator.send(zmq::message_t{&threadpool_resize, sizeof(update_ms)}, zmq::send_flags::none);

                            // Update worker B thread size
                            orchestrator.send(zmq::message_t {topics::workerb_topic()}, zmq::send_flags::sndmore);
                            orchestrator.send(zmq::message_t{&threadpool_resize, sizeof(update_ms)}, zmq::send_flags::none);
                            
                            Metrics::instance().observe_batch_duration(std::stod(value));
                            LOG_DEBUG("main", "batch duration: " + value + "s");
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

    orchestrator.send(zmq::message_t(topics::global_topic()), zmq::send_flags::sndmore);
    orchestrator.send(zmq::message_t(messages::SHUTDOWN, 8), zmq::send_flags::none);
    
    // Wait for all children to finish before closing sockets
    // IMPORTANT waitpid could return -1 with ECHILD errno if sigchild was previusly handeld, here is voluntarily ignored
    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(p_child[i], nullptr, 0);

    // Shutdown get handler thread
    metrics_exposer.stop();
    metrics_thread.join();

    return 0;
}
