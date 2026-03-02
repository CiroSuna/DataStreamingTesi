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

        orchestrator_sub.set(zmq::sockopt::subscribe, topics::GLOBAL);
        recv_from_workerB.set(zmq::sockopt::rcvtimeo, 100);
        orchestrator_sub.set(zmq::sockopt::rcvtimeo, 100);
        std::cout << "Sink: All connections established\n";
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

    zmq::pollitem_t items[] = {
        { recv_from_workerB, 0, ZMQ_POLLIN, 0 },
        { orchestrator_sub,  0, ZMQ_POLLIN, 0 }
    };

    try {
        while (true) {
            zmq::poll(items, 2);
 
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t msg;

                auto status = recv_from_workerB.recv(msg, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue;

                data d {};
                memcpy(&d, msg.data(), sizeof(data));
                
                std::string res_message {};
                if (d.curr_value == d.original_value) res_message = "Dato consinstente!";
                else res_message = "Dato non consistente";

                std::cout << "Sink: received data " << d.curr_value << " " << res_message << '\n' << std::flush;

                // Notify orchestrator and wait for ack
                /*
                sync_socket.send(zmq::message_t("END", 3), zmq::send_flags::none);
                zmq::message_t ack;
                sync_socket.recv(ack);
                break;
                */
            }

            if (items[1].revents & ZMQ_POLLIN) {
                zmq::message_t topic;
                zmq::message_t msg;

                auto status = orchestrator_sub.recv(topic, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue;

                status = orchestrator_sub.recv(msg);
                if (!status.has_value()) continue;

                std::string r {static_cast<char*>(msg.data()), msg.size()};
                if (r == "SHUTDOWN") {
                    break;
                }
            }
        }
    }
    catch (zmq::error_t& e) {
        std::cerr << "Sink: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    orchestrator_sub.close();
    sync_socket.close();
    recv_from_workerB.close();
    ctx.shutdown();
    ctx.close();
    return 0;
}
