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

    int batch_size {50};
    constexpr int expected_batches {50};
    int received {0};
    int batches_recived {0};
    int64_t batch_start_time {0};

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

                LOG_DEBUG("sink", "received data " + std::to_string(d.curr_value) + " " + res_message);

                if (received == 0 && d.send_time != 0)
                    batch_start_time = d.send_time;

                if (++received == batch_size) {
                    batches_recived++;
                    received = 0;
                    LOG_INFO("sink", "Batch completed (" + std::to_string(batches_recived) + "/" + std::to_string(expected_batches) + ")");

                    if (batch_start_time != 0) {
                        double duration = (std::chrono::steady_clock::now().time_since_epoch().count() - batch_start_time) * 1e-9;
                        std::string dur_str = std::to_string(duration);
                        orchestrator_dealer.send(zmq::message_t{msg_types::BATCH_DURATION, std::strlen(msg_types::BATCH_DURATION)}, zmq::send_flags::sndmore);
                        orchestrator_dealer.send(zmq::message_t{dur_str.data(), dur_str.size()}, zmq::send_flags::none);
                        batch_start_time = 0;
                    } else {
                        orchestrator_dealer.send(zmq::message_t{msg_types::BATCH_FINISHED, std::strlen(msg_types::BATCH_FINISHED)}, zmq::send_flags::sndmore);
                        orchestrator_dealer.send(zmq::message_t{}, zmq::send_flags::none);
                    }
                }

                if (batches_recived >= expected_batches) {
                    // Notify orchestrator and wait for ack
                    sync_socket.send(zmq::message_t(messages::END, 3), zmq::send_flags::none);
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
