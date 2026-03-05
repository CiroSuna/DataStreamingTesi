#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"

constexpr int PIPE_LENGTH {4};

// Flag to tell when to shutdown pipe
std::atomic<bool> shutdown {false};

// Exiting on error function
[[noreturn]] void fatal(const std::string& msg) {
    std::cerr << msg << '\n';
    kill(0, SIGTERM);

    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(-1, nullptr, 0);

    exit(EXIT_FAILURE);
}

pid_t process_starter(const std::string& process_name, const std::string& in_socket, const std::string& out_socket, const std::string& orchestrator_ipc){
    pid_t new_proc {fork()};
    if (new_proc == -1) {
        fatal("Errore fork " + process_name + ": " + strerror(errno));
    }
    else if (new_proc == 0) {
        std::string path {"./" + process_name + "/" + process_name};
        execl(path.c_str(), process_name.c_str(), in_socket.c_str(), out_socket.c_str(), orchestrator_ipc.c_str(), nullptr);
        perror(("Errore execl " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
    
    return new_proc;
}

void on_sigchld(int) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        std::cerr << "Child " << pid << " died unexpectedly, shutting down\n";
        kill(0, SIGTERM);
    }
}

void on_sigint(int) {
    kill(0, SIGTERM);
    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(-1, nullptr, 0);
}

// Wait for `expected` READY messages from child processes, reply GO to each
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
            std::cout << "[DEBUG] Received: " << r << " (" << ready_count + 1 << "/" << expected << ")\n" << std::flush;
            if (r == messages::READY) {
                ++ready_count;
                std::cout << "Worker ready (" << ready_count << "/" << expected << ")\n" << std::flush;
                sync_socket.send(zmq::message_t(messages::GO, 2), zmq::send_flags::none);
            }
        }
        catch (const zmq::error_t& e) {
            fatal("Timeout waiting for READY messages. Got " + std::to_string(ready_count) + "/" + std::to_string(expected));
        }
    }
}

int main(void){
    signal(SIGINT, on_sigint);
    signal(SIGCHLD, on_sigchld);

    // Cleanup for old ipc files in tmp
    cleanup_ipc_path(ipc_paths::orchestrator());
    cleanup_ipc_path(ipc_paths::sync_socket_path());
    cleanup_ipc_path(ipc_paths::sender_to_workerA());
    cleanup_ipc_path(ipc_paths::workerA_to_workerB());
    cleanup_ipc_path(ipc_paths::workerB_to_sink());

    pid_t p_child[PIPE_LENGTH] {
        process_starter("workerA", ipc_paths::sender_to_workerA(), ipc_paths::workerA_to_workerB(), ipc_paths::orchestrator()),
        process_starter("workerB", ipc_paths::workerA_to_workerB(), ipc_paths::workerB_to_sink(), ipc_paths::orchestrator()),
        process_starter("sink", ipc_paths::workerB_to_sink(), "", ipc_paths::orchestrator()),
        process_starter("sender", "", ipc_paths::sender_to_workerA(), ipc_paths::orchestrator())
    };

    // Starting context, sockets and binding FIRST
    zmq::context_t ctx {};
    zmq::socket_t orchestrator {ctx, zmq::socket_type::pub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::rep};

    try {
        orchestrator.bind(ipc_paths::orchestrator());
        sync_socket.bind(ipc_paths::sync_socket_path());
        sync_socket.set(zmq::sockopt::rcvtimeo, 5000);
    }
    catch (const zmq::error_t& e) {
        fatal(std::string("orchestrator: ") + e.what());
    }

    // Sync 
    std::cout << "Main: Starting to wait for READY messages...\n" << std::flush;
    wait_for_ready(sync_socket, PIPE_LENGTH);
    std::cout << "All process sync and in exection\n";

    sync_socket.set(zmq::sockopt::rcvtimeo, -1);
    zmq::message_t msg_final;
    auto res_final = sync_socket.recv(msg_final, zmq::recv_flags::none);
    if (!res_final.has_value()) {
        fatal("Error: timeout waiting for final message from sink");
    }

    std::string final_str {static_cast<char*>(msg_final.data()), msg_final.size()};

    if (final_str != messages::END) {
        fatal("Error: wrong message received, expected: END, got: " + final_str);
    }

    std::cout << "sink: " << final_str << " dato arrivato al sink\n";
    sync_socket.send(zmq::message_t(messages::OK, 2), zmq::send_flags::none);
    
    orchestrator.send(zmq::message_t(topics::global_topic()), zmq::send_flags::sndmore);
    orchestrator.send(zmq::message_t(messages::SHUTDOWN, 8), zmq::send_flags::none);

    // Wait for all children to finish before closing sockets
    // IMPORTANT waitpid could return -1 with ECHILD errno if sigchild was previusly handeld, here is voluntarily ignored
    for (size_t i {0}; i < PIPE_LENGTH; i++)
        waitpid(p_child[i], nullptr, 0);

    sync_socket.close();
    orchestrator.close();
    return 0;
}
