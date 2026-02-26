#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"

void process_starter(const std::string& process_name, const std::string& in_socket, const std::string& out_socket, const std::string& orchestrator_ipc){
    pid_t new_proc {fork()};
    if (new_proc == -1) {
        perror(("Errore fork " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
    else if (new_proc == 0) {
        std::string path {"./" + process_name + "/" + process_name};
        execl(path.c_str(), process_name.c_str(), in_socket.c_str(), out_socket.c_str(), orchestrator_ipc.c_str(), nullptr);
        perror(("Errore execl " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
}

void on_sigint(int) {
    kill(0, SIGTERM);
}

int main(int argc, char const *argv[]){
    signal(SIGINT, on_sigint);

    // Cleanup for old ipc files in tmp
    cleanup_ipc_path(ipc_paths::orchestrator());
    cleanup_ipc_path(ipc_paths::sync_socket_path());
    cleanup_ipc_path(ipc_paths::sender_to_workerA());
    cleanup_ipc_path(ipc_paths::workerA_to_workerB());
    cleanup_ipc_path(ipc_paths::workerB_to_sink());

    process_starter("workerA", ipc_paths::sender_to_workerA(), ipc_paths::workerA_to_workerB(), ipc_paths::orchestrator());
    process_starter("workerB", ipc_paths::workerA_to_workerB(), ipc_paths::workerB_to_sink(), ipc_paths::orchestrator());
    process_starter("sink", ipc_paths::workerB_to_sink(), "", ipc_paths::orchestrator());
    process_starter("sender", "", ipc_paths::sender_to_workerA(), ipc_paths::orchestrator()); 


    // Starting context, sockets and binding FIRST
    zmq::context_t ctx {};
    zmq::socket_t orchestrator {ctx, zmq::socket_type::pub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::rep};

    try {
        orchestrator.bind(ipc_paths::orchestrator());
        sync_socket.bind(ipc_paths::sync_socket_path());
        
        // Set receive timeout to avoid deadlock (5 seconds)
        sync_socket.set(zmq::sockopt::rcvtimeo, 5000);
    }
    catch (const zmq::error_t& e) {
        std::cerr << "orchestrator: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    
    std::cout << "Main: Starting to wait for READY messages...\n" << std::flush;

    // TODO: evaluate to put sync logic inside a funciotn for more code readability
    // SYNC logic
    constexpr int expected {4};
    int ready_count {0};
    while (ready_count < expected) {
        zmq::message_t msg;
        try {
            auto res = sync_socket.recv(msg, zmq::recv_flags::none);
            if (!res.has_value()) {
                std::cerr << "Timeout waiting for READY messages. Got " << ready_count << "/" << expected << "\n";
                exit(EXIT_FAILURE);
            }
            std::string r {static_cast<char*>(msg.data()), msg.size()};
            std::cout << "[DEBUG] Received: " << r << " (" << ready_count + 1 << "/" << expected << ")\n" << std::flush;
            if (r == "READY") {
                ++ready_count;
                std::cout << "Worker ready (" << ready_count << "/" << expected << ")\n" << std::flush;
                sync_socket.send(zmq::message_t("GO", 2), zmq::send_flags::none);
            }
        }
        catch (const zmq::error_t& e) {
            std::cerr << "Timeout waiting for READY messages. Got " << ready_count << "/" << expected << "\n";
            exit(EXIT_FAILURE);
        }
    }
    // End sync logic

    
    std::cout << "All process sync and in exection\n";

    // Remove timeout for the final blocking recv from sink
    // sync_socket.set(zmq::sockopt::rcvtimeo, -1);

    zmq::message_t msg_final;
    auto res_final = sync_socket.recv(msg_final, zmq::recv_flags::none);
    if (!res_final.has_value()) {
        std::cerr << "Error: no final message from sink\n";
        exit(EXIT_FAILURE);
    }
    std::string final_str {static_cast<char*>(msg_final.data()), msg_final.size()};
    std::cout << "sink: " << final_str << " dato arrivato al sink\n";
    sync_socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);

    sync_socket.close(); 
    orchestrator.close();
    return 0;
}
