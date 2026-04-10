#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>
#include <zmq.hpp>
#include <unistd.h>
#include "dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"



int main() {
    Logger::instance().init(std::string(LOG_DIR) + "/sender.log");
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t send_to_A {ctx, zmq::socket_type::push};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    zmq::socket_t orchestrator_dealer {ctx, zmq::socket_type::dealer}; 
    
    std::string routing_id {topics::SENDER};
    
    try {
        orchestrator_sub.connect(ipc_paths::orchestrator());
        send_to_A.connect(ipc_paths::sender_to_workerA());
        sync_socket.connect(ipc_paths::sync_socket_path());
        orchestrator_dealer.set(zmq::sockopt::routing_id, routing_id);
        orchestrator_dealer.connect(ipc_paths::router_path());
        orchestrator_sub.set(zmq::sockopt::subscribe, topics::GLOBAL);
    }
    catch (zmq::error_t& e) {
        std::cerr << "sender: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    if (!sync_with_orchestrator(sync_socket, std::string("sender"))) {
        exit(EXIT_FAILURE);
    }

    // TEST send
    /*
    data d{10};
    send_to_A.send(zmq::message_t(&d, sizeof(data)), zmq::send_flags::none);
    std::cout << "Sender: dato mandato verso A\n" << std::flush;
    */

    zmq::pollitem_t items[] = {
        { orchestrator_sub, 0, ZMQ_POLLIN,  0 },
        { send_to_A,        0, ZMQ_POLLOUT, 0 }
    };

    int curr_value {10};

    try {
        while (true) {
            zmq::poll(items, 2, std::chrono::milliseconds(-1));

            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t topic;
                zmq::message_t msg;

                auto status = orchestrator_sub.recv(topic, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue;

                status = orchestrator_sub.recv(msg);
                if (!status.has_value()) continue;

                std::string r {static_cast<char*>(msg.data()), msg.size()};
                if (r == messages::SHUTDOWN) {
                    break;
                }
            }

            if (items[1].revents & ZMQ_POLLOUT) {


                for (size_t i {0}; i < 100; i++) {
                    data d{curr_value++};
                    d.send_time = std::chrono::steady_clock::now().time_since_epoch().count();
                    send_to_A.send(zmq::message_t(&d, sizeof(data)), zmq::send_flags::none);
                    LOG_DEBUG("sender", "dato mandato verso A: " + std::to_string(d.curr_value));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
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
    orchestrator_dealer.close();
    ctx.shutdown();
    ctx.close();
    return 0;
}
