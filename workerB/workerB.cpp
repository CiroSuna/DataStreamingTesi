#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include "threadPool.hpp"
#include "dataTypes.hpp"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: workerB <ipc_filepath>\n";
        return 1;
    }
    
    
    std::string ipc_filepath {argv[1]};
    std::string to_sink_path {argv[2]};
    std::string orchestrator_ipc {argv[3]}; 
    
    zmq::context_t ctx {};
    zmq::socket_t rcv_from_A {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to_sink {ctx, zmq::socket_type::push};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};

    std::remove(to_sink_path.c_str());
    send_to_sink.bind(to_sink_path);
    rcv_from_A.connect(ipc_filepath);
    orchestrator_sub.set(zmq::sockopt::subscribe, "[WorkB]");
    orchestrator_sub.connect(orchestrator_ipc);



    zmq::pollitem_t items[] {
        {rcv_from_A, 0, ZMQ_POLLIN, 0},
        {orchestrator_sub, 0, ZMQ_POLLIN, 0}
    };
    
    
    while (true){
        zmq::message_t msg {sizeof(data)};
        zmq::message_t orchestrator_msg {sizeof(update_ms)};
        zmq::poll(items, 2, std::chrono::seconds(1));

        if (items[0].revents & ZMQ_POLLIN) {
            rcv_from_A.recv(msg);

            data d {};
            std::memcpy(&d, msg.data(), sizeof(data));

            std::cout << "B: Dato ricevuto da sender: " << d.curr_value << '\n';

            send_to_sink.send(msg, zmq::send_flags::none);

        }
        if (items[1].revents & ZMQ_POLLIN) {

            zmq::message_t topic {sizeof(update_ms)};
            
            orchestrator_sub.recv(topic, zmq::recv_flags::none);
            
            // Verifica se c'è un secondo frame (multipart message)
            if (orchestrator_sub.get(zmq::sockopt::rcvmore)) {
                orchestrator_sub.recv(msg, zmq::recv_flags::none);
                
                update_ms up {};
                std::memcpy(&up, msg.data(), sizeof(update_ms));
                std::cout << "Tipo=" << up.t << " Resize=" << up.resize << '\n';
            }
        }

    }

    send_to_sink.close();
    rcv_from_A.close(); 
    return 0;
}
