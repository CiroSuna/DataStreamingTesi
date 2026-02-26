#include <iostream>
#include <zmq.hpp>
#include "dataTypes.hpp"
#include "utils.hpp"


int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: sink <ipc_filepath> <unused> <orchestrator_ipc>\n";
        return 1;
    }

    std::string ipc_filepath {argv[1]};

    std::string orchestrator_ipc {argv[3]};

    std::cout << "Sink: Starting...\n";
    
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    zmq::socket_t recv_from_workerB {ctx, zmq::socket_type::pull};

    std::cout << "Sink: Sockets created\n";

    try {
        orchestrator_sub.connect(orchestrator_ipc);
        sync_socket.connect(ipc_paths::sync_socket_path());
        recv_from_workerB.bind(ipc_filepath);
        std::cout << "Sink: All connections established\n";

        // Set topic
        // orchestrator_sub.set(zmq::sockopt::subscribe, topics::SINK);
    }
    catch (const zmq::error_t& e) {
        std::cerr << "sink: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    // Sync with orchestrator
    if (!sync_with_orchestrator(sync_socket, "sink")) {
        exit(EXIT_FAILURE);
    }

    std::cout << "Sink: synchronized and ready\n";

    zmq::message_t msg {};
    recv_from_workerB.recv(msg);
    data d;
    memcpy(&d, msg.data(), sizeof(data));
    std::cout << "Sink: received data = " << d.curr_value << '\n' << std::flush;
    
    // REQ must recv a reply after the final send
    sync_socket.send(zmq::message_t("END", 3), zmq::send_flags::none);
    zmq::message_t ack;
    sync_socket.recv(ack);
    
    
    orchestrator_sub.close();
    sync_socket.close();
    recv_from_workerB.close();
    return 0;
}
