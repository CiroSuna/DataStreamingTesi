#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>
#include <cmath>
#include <zmq.hpp>
#include <unistd.h>
#include <random>
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
    
    LOG_INFO("sender", "Sender sta iniziando eseczione");
    try {
        orchestrator_sub.connect(ipc_paths::orchestrator());
        send_to_A.connect(ipc_paths::sender_to_workerA());
        sync_socket.connect(ipc_paths::sync_socket_path());
        orchestrator_dealer.set(zmq::sockopt::routing_id, routing_id);
        orchestrator_dealer.connect(ipc_paths::router_path());
        orchestrator_sub.set(zmq::sockopt::subscribe, topics::GLOBAL);
        orchestrator_sub.set(zmq::sockopt::subscribe, topics::SENDER);
    }
    catch (zmq::error_t& e) {
        std::cerr << "sender: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    if (!sync_with_orchestrator(sync_socket, std::string("sender"))) {
        exit(EXIT_FAILURE);
    }

    zmq::pollitem_t items[] = {
        { orchestrator_sub, 0, ZMQ_POLLIN, 0 },
        { send_to_A, 0, ZMQ_POLLOUT, 0 }
    };

    constexpr double base_rate_ms {2.2};   // mean inter-arrival base (ms) -> ~200 items/s
    constexpr double amplitude_ms {0.5};   // swing: [2ms, 8ms] -> [~500, ~125] items/s
    constexpr double period_s {15.0};  // one full wave every 20s (3 waves in 60s)
    constexpr double run_duration_s {70.0};

    int curr_value {10};
    int64_t last_send_time {0};
    int64_t now {0};
    int64_t inter_arrival {0};
    std::mt19937 rand {std::random_device{}()};
    std::exponential_distribution<double> dist {1.0 / base_rate_ms};
    auto start_time = std::chrono::steady_clock::now();
    bool generation_done_notified {false};
    bool generating {true};

    try {
        while (true) {
            zmq::poll(items, 2, std::chrono::milliseconds(-1));

            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t topic;
                zmq::message_t msg_type;
                zmq::message_t msg;
                
                auto status = orchestrator_sub.recv(topic, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue;

                status = orchestrator_sub.recv(msg_type, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue;

                status = orchestrator_sub.recv(msg);
                if (!status.has_value()) continue;

                std::string topic_str {static_cast<char*>(topic.data()), topic.size()};
                std::string msg_type_str {static_cast<char*>(msg_type.data()), msg_type.size()};
                std::string value_str {static_cast<char*>(msg.data()), msg.size()}; 

                if (msg_type_str == msg_types::RATE_UPDATE) {
                    int value = std::stod(value_str); 
                    if (value > 0) {
                        dist.param(std::exponential_distribution<double>::param_type(1.0 / value));
                    }
                }

                if (msg_type_str == messages::SHUTDOWN) {
                    break;
                }
            }

            double elapsed_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start_time).count();
            if (generating && elapsed_s >= run_duration_s) {
                generating = false;
                if (!generation_done_notified) {
                    orchestrator_dealer.send(zmq_str(msg_types::SENDER_DONE), zmq::send_flags::sndmore);
                    orchestrator_dealer.send(zmq_str("1"), zmq::send_flags::none);
                    generation_done_notified = true;
                    LOG_INFO("sender", "Run timer reached, sender stopped generating new items");
                }
            }

            if (generating && (items[1].revents & ZMQ_POLLOUT)) {

                data d{curr_value++};

                now = std::chrono::steady_clock::now().time_since_epoch().count();
                if (last_send_time > 0.0) {
                    inter_arrival = now - last_send_time;
                }
                last_send_time = now;
                // Send to orchestrator lambda update
                if (inter_arrival > 0) {
                    orchestrator_dealer.send(zmq_str(msg_types::LAMBDA_UPDATE), zmq::send_flags::sndmore);
                    orchestrator_dealer.send(zmq::buffer(std::to_string(inter_arrival)), zmq::send_flags::none);
                }
                d.send_time = std::chrono::steady_clock::now().time_since_epoch().count();
                send_to_A.send(zmq::message_t(&d, sizeof(data)), zmq::send_flags::none);
                LOG_DEBUG("sender", "dato mandato verso A: " + std::to_string(d.curr_value));

                // Sinusoidal rate update
                double current_rate_ms = base_rate_ms
                    + amplitude_ms * std::sin(2.0 * M_PI * elapsed_s / period_s);
                if (current_rate_ms < 1.0) current_rate_ms = 1.0;
                dist.param(std::exponential_distribution<double>::param_type(1.0 / current_rate_ms));

                double wait_ms = dist(rand);
                if (wait_ms < 1.0) wait_ms = 1.0;
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(std::round(wait_ms))));
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
