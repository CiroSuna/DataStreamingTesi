#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include <unistd.h>
#include "dataTypes.hpp"
#include "utils.hpp"



int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << ("Arguments not well formed");
        return 1;
    }
    
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t send_to_A {ctx, zmq::socket_type::push};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    
    try {
        orchestrator_sub.connect(ipc_paths::orchestrator());
        send_to_A.connect(ipc_paths::sender_to_workerA());
        sync_socket.connect(ipc_paths::sync_socket_path());

        orchestrator_sub.set(zmq::sockopt::subscribe, topics::GLOBAL);
        orchestrator_sub.set(zmq::sockopt::rcvtimeo, 100);
    }
    catch (zmq::error_t& e) {
        std::cerr << "sender: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    if (!sync_with_orchestrator(sync_socket, std::string("sender"))) {
        exit(EXIT_FAILURE);
    }

    // TEST send
    data d{10};
    send_to_A.send(zmq::message_t(&d, sizeof(data)), zmq::send_flags::none);
    std::cout << "Sender: dato mandato verso A\n" << std::flush;

    zmq::pollitem_t items[] = {
        { orchestrator_sub, 0, ZMQ_POLLIN, 0 }
    };

    try {
        while (true) {
            zmq::poll(items, 1, std::chrono::milliseconds(100));

            if (items[0].revents & ZMQ_POLLIN) {
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
        std::cerr << "Sender: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    orchestrator_sub.close();
    send_to_A.close();
    sync_socket.close();
    ctx.shutdown();
    ctx.close();
    return 0;
}
