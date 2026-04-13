#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <zmq.hpp>
#include "threadPool.hpp"
#include "dataTypes.hpp"
#include "utils.hpp"
#include "logger.hpp"

int fib(int n) {
    if (n <= 1) return n;
    int a{0}, b{1};
    for (int i{2}; i <= n; i++) {
        int tmp = a + b;
        a = b;
        b = tmp;
    }
    return b;
}

constexpr const int inital_pool_threads = 10;

int main() {
    Logger::instance().init(std::string(LOG_DIR) + "/workerA.log");
    LOG_INFO("workerA", "Starting...");
    
    zmq::context_t ctx {};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};
    zmq::socket_t sync_socket {ctx, zmq::socket_type::req};
    zmq::socket_t recv_from_sender {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to_workerB {ctx, zmq::socket_type::push};
    zmq::socket_t orchestrator_dealer {ctx, zmq::socket_type::dealer};

    LOG_DEBUG("workerA", "Sockets created");

    try {
        orchestrator_sub.connect(ipc_paths::orchestrator());
        sync_socket.connect(ipc_paths::sync_socket_path());
        recv_from_sender.bind(ipc_paths::sender_to_workerA());
        send_to_workerB.connect(ipc_paths::workerA_to_workerB());
        orchestrator_dealer.set(zmq::sockopt::routing_id, topics::WORKERA);
        orchestrator_dealer.connect(ipc_paths::router_path());

        // Set topic
        orchestrator_sub.set(zmq::sockopt::subscribe, topics::WORKERA);
        orchestrator_sub.set(zmq::sockopt::subscribe, topics::GLOBAL);
        LOG_INFO("workerA", "All connections established");
    }
    catch (const zmq::error_t& e) {
        std::cerr << "workerA: " << e.what() << '\n';
        exit(EXIT_FAILURE);
    }

    // Sync with orchestrator
    if (!sync_with_orchestrator(sync_socket, "workerA")) {
        exit(EXIT_FAILURE);
    }

    LOG_INFO("workerA", "Synchronized and ready");

    
    ThreadPool pool {inital_pool_threads};
    LOG_INFO("WorkerA", "initial numebr of threads in worker A threadpool is: " + std::to_string(inital_pool_threads));

    try {
        orchestrator_dealer.send(zmq_str(msg_types::THREAD_INC), zmq::send_flags::sndmore);
        orchestrator_dealer.send(zmq_str("10"), zmq::send_flags::none);
    }
    catch(zmq::error_t& e) {
        LOG_INFO("WorkerA", std::string("workerA: failed to send THRAD_INC to orchestrator: ") + e.what());
        exit(EXIT_FAILURE);
    }

    std::mutex result_queue_lock;
    std::queue<data> result_queue;

    zmq::pollitem_t items[] = {
        { recv_from_sender, 0, ZMQ_POLLIN, 0 },
        { orchestrator_sub , 0, ZMQ_POLLIN, 0 }
    };

    try {

        while (true) {
            zmq::poll(items, 2, std::chrono::milliseconds(50));
            
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t msg;
                
                auto status = recv_from_sender.recv(msg, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue; 
                
                data d {};
                memcpy(&d, msg.data(), sizeof(data));
                LOG_DEBUG("workerA", "dato ricevuto da sender: " + std::to_string(d.curr_value));

                pool.add_task([d, &result_queue, &result_queue_lock](){
                    std::ostringstream tid_ss;
                    tid_ss << std::this_thread::get_id();
                    LOG_DEBUG("workerA", "Thread " + tid_ss.str() + " processing value: " + std::to_string(d.curr_value));

                    int64_t t_start = std::chrono::steady_clock::now().time_since_epoch().count();
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    data processed {d};
                    processed.curr_value += fib(processed.original_value % 30);
                    processed.workerA_time = std::chrono::steady_clock::now().time_since_epoch().count();
                    processed.workerA_service_time = processed.workerA_time - t_start;
                    LOG_DEBUG("workerA", "Thread " + tid_ss.str() + " done -> " + std::to_string(processed.curr_value));

                    std::unique_lock<std::mutex> lock(result_queue_lock);
                    result_queue.push(processed);
                });

                //send_to_workerB.send(zmq::message_t(&d, sizeof(data)), zmq::send_flags::none);
            }
            
            if (items[1].revents & ZMQ_POLLIN) {
                
                
                zmq::message_t topic;
                zmq::message_t msg;
                
                auto status = orchestrator_sub.recv(topic, zmq::recv_flags::dontwait);
                if (!status.has_value()) continue;
                
                status = orchestrator_sub.recv(msg);
                if (!status.has_value()) continue; 

                update_ms update {};
                if(std::string {static_cast<char*>(topic.data()), topic.size()} == topics::workera_topic()) {
                    memcpy(&update, msg.data(), sizeof(update_ms));
                    std::string msg_type;
                    std::string action;
                    switch (update.t) {
                        case update_type::THREAD_INC:
                            pool.add_n_threads(update.resize);
                            msg_type = msg_types::THREAD_INC;
                            action = "incremented";
                            break;
                        case update_type::THREAD_DEC:
                            pool.destroy_n_threads(update.resize);
                            msg_type = msg_types::THREAD_DEC;
                            action = "decremented";
                            break;
                        default:
                            LOG_INFO("WorkerA", "Invalid threadpool resize value");
                            continue;
                    }
                    LOG_INFO("WorkerA", "Threadpool " + action + " by: " + std::to_string(update.resize));
                    std::string resize_val {std::to_string(update.resize)};
                    orchestrator_dealer.send(zmq_str(msg_type.c_str()), zmq::send_flags::sndmore);
                    orchestrator_dealer.send(zmq_str(resize_val.c_str()), zmq::send_flags::none);
                }
                std::string r {static_cast<char*>(msg.data()), msg.size()};
                if (r == messages::SHUTDOWN) {
                    LOG_INFO("workerA", "Shutdown received, proceeding to close");
                    break;
                }
            }

            {
                std::unique_lock<std::mutex> lock(result_queue_lock);
                while (!result_queue.empty()) {
                    auto res {result_queue.front()};
                    result_queue.pop();
                    send_to_workerB.send(zmq::message_t(&res, sizeof(data)), zmq::send_flags::none);
                }
            }
        }
    }
    catch (zmq::error_t& e) {
        std::cerr << "WorkerA: " << + e.what() << '\n';
        exit(EXIT_FAILURE);
    }
    
    orchestrator_sub.close();
    sync_socket.close();
    recv_from_sender.close();
    send_to_workerB.close();
    orchestrator_dealer.close();
    ctx.shutdown();
    ctx.close();

    return 0;
}
