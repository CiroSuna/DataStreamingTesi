#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: workerA <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath {argv[1]};
    std::string to_workerB_path {argv[2]}; 
    std::string orchestrator_ipc_sub {argv[3]};


    std::cout << "WorkerA: Starting...\n";
    
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    zmq::socket_t recv_from_sender {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to_workerB {ctx, zmq::socket_type::push};

    std::cout << "WorkerA: Sockets created\n";

    try {
        orchestrator_sub.connect(orchestrator_ipc_sub);
        sync_socket.connect(ipc_paths::sync_socket_path());
        recv_from_sender.bind(ipc_filepath);
        send_to_workerB.connect(to_workerB_path);

        // Set topic
        orchestrator_sub.set(zmq::sockopt::subscribe, topics::WORKERA);
        std::cout << "WorkerA: All connections established\n" << std::flush;
    }
    catch (const zmq::error_t& e) {
        std::cerr << "workerA: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    // Sync with orchestrator
    if (!sync_with_orchestrator(sync_socket, "workerA")) {
        exit(EXIT_FAILURE);
    }

    std::cout << "WorkerA: synchronized and ready\n";

    zmq::message_t msg {};
    recv_from_sender.recv(msg);
    data d;
    memcpy(&d, msg.data(), sizeof(data));
    std::cout << "WorkerA: received data = " << d.curr_value << '\n' << std::flush;
    send_to_workerB.send(msg, zmq::send_flags::none);


    orchestrator_sub.close();
    sync_socket.close();
    recv_from_sender.close();
    send_to_workerB.close();
    return 0;
}
