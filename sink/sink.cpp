#include <iostream>
#include <zmq.hpp>
#include <chrono>
#include <cstring>
#include "dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"


int main() {
    Logger::instance().init(std::string(LOG_DIR) + "/sink.log");
    LOG_INFO("sink", "Starting...");
    
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    zmq::socket_t recv_from_workerB {ctx, zmq::socket_type::pull};
    zmq::socket_t orchestrator_dealer {ctx, zmq::socket_type::dealer};

    LOG_DEBUG("sink", "Sockets created");

    try {
        orchestrator_sub.connect(ipc_paths::orchestrator());
        sync_socket.connect(ipc_paths::sync_socket_path());
        recv_from_workerB.bind(ipc_paths::workerB_to_sink());
        orchestrator_dealer.set(zmq::sockopt::routing_id, topics::SINK);
        orchestrator_dealer.connect(ipc_paths::router_path());

        orchestrator_sub.set(zmq::sockopt::subscribe, topics::GLOBAL);
        LOG_INFO("sink", "All connections established");
    }
    catch (const zmq::error_t& e) {
        std::cerr << "sink: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    // Sync with orchestrator
    if (!sync_with_orchestrator(sync_socket, "sink")) {
        exit(EXIT_FAILURE);
    }

    LOG_INFO("sink", "Synchronized and ready");

    int recived_data {0};
    constexpr int expected_data {50000};

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
                
                int64_t arrival_time = std::chrono::steady_clock::now().time_since_epoch().count();

                std::string res_message {};
                if (d.curr_value == d.original_value) res_message = "Dato consinstente!";
                else res_message = "Dato non consistente";

                LOG_DEBUG("sink", "received data " + std::to_string(d.curr_value) + " " + res_message);

                if (d.send_time != 0 && d.workerA_time != 0 && d.workerB_time != 0) {
                    item_latency lat;
                    lat.sender_to_A = (d.workerA_time - d.send_time)    * 1e-9;
                    lat.A_to_B      = (d.workerB_time - d.workerA_time) * 1e-9;
                    lat.B_to_sink   = (arrival_time   - d.workerB_time) * 1e-9;
                    lat.end_to_end  = (arrival_time   - d.send_time)    * 1e-9;
                    LOG_DEBUG("sink", "latency [sender->A=" + std::to_string(lat.sender_to_A) +
                              "s A->B=" + std::to_string(lat.A_to_B) +
                              "s B->sink=" + std::to_string(lat.B_to_sink) +
                              "s end-to-end=" + std::to_string(lat.end_to_end) + "s]");
                    orchestrator_dealer.send(zmq_str(msg_types::ITEM_LATENCY), zmq::send_flags::sndmore);
                    orchestrator_dealer.send(zmq::message_t{&lat, sizeof(item_latency)}, zmq::send_flags::none);
                }


                if (++recived_data >= expected_data){
                    // Notify orchestrator and wait for ack
                    sync_socket.send(zmq_str(messages::END), zmq::send_flags::none);
                    zmq::message_t ack;
                    (void)sync_socket.recv(ack, zmq::recv_flags::none);
                    break;
                }
            }

            if (items[1].revents & ZMQ_POLLIN) {
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
        }
    }
    catch (zmq::error_t& e) {
        std::cerr << "Sink: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    orchestrator_sub.close();
    sync_socket.close();
    recv_from_workerB.close();
    orchestrator_dealer.close();
    ctx.shutdown();
    ctx.close();
    return 0;
}
