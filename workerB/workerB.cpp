#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: workerB <ipc_filepath>\n";
        return 1;
    }
    
    
    std::string ipc_filepath {argv[1]};
    std::string to_sink_path {argv[2]};
    std::string orchestrator_ipc {argv[3]}; 

    std::cout << "WorkerB: Starting...\n";
    
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    zmq::socket_t recv_from_workerA {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to_sink {ctx, zmq::socket_type::push};

    std::cout << "WorkerB: Sockets created\n";

    try {
        orchestrator_sub.connect(orchestrator_ipc);
        sync_socket.connect(ipc_paths::sync_socket_path());
        recv_from_workerA.bind(ipc_filepath);
        send_to_sink.connect(to_sink_path);
        std::cout << "WorkerB: All connections established\n";
    }
    catch (const zmq::error_t& e) {
        std::cerr << "workerB: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    // Sync with orchestrator
    if (!sync_with_orchestrator(sync_socket, "workerB")) {
        exit(EXIT_FAILURE);
    }

    std::cout << "WorkerB: synchronized and ready\n";

    zmq::message_t msg {};
    recv_from_workerA.recv(msg);
    data d;
    memcpy(&d, msg.data(), sizeof(data));
    std::cout << "WorkerB: received data = " << d.curr_value << '\n' << std::flush;
    send_to_sink.send(msg, zmq::send_flags::none);

    orchestrator_sub.close();
    sync_socket.close();
    recv_from_workerA.close();
    send_to_sink.close();
    
    return 0;
}
