#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include "threadPool.hpp"
#include "dataTypes.hpp"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: workerA <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath {argv[1]};
    std::string to_workerB_path {argv[2]}; 
    std::string orchestrator_ipc_sub {argv[3]};

    zmq::context_t ctx {};
    zmq::socket_t rcv_from {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to {ctx, zmq::socket_type::push};
    zmq::socket_t orchestrator_sub {ctx, zmq::socket_type::sub};

    std::remove(to_workerB_path.c_str());
    send_to.bind(to_workerB_path);
    rcv_from.connect(ipc_filepath);
    orchestrator_sub.set(zmq::sockopt::subscribe, "[WorkA]");
    orchestrator_sub.connect(orchestrator_ipc_sub);

    // TODO: Implementare logica workerA con threadPool
   
    
    zmq::pollitem_t items[] {
        {rcv_from, 0, ZMQ_POLLIN, 0},
        {orchestrator_sub, 0, ZMQ_POLLIN, 0}
    };

    // Poll loop
    while (true){
        zmq::message_t msg {sizeof(data)};
        zmq::message_t orchestrator_msg {sizeof(update_ms)};
        zmq::poll(items, 2, std::chrono::seconds(1));

        if (items[0].revents & ZMQ_POLLIN) {
            rcv_from.recv(msg);
            std::cout << "A: messaggio ricevuto da sender\n";
            data d{}; 
            std::memcpy(&d, msg.data(), sizeof(data));

            std::cout << "A: Dato ricevuto da sender: " << d.curr_value << '\n';

            send_to.send(msg, zmq::send_flags::none);

        }
        if (items[1].revents & ZMQ_POLLIN) {

            zmq::message_t topic {};
            
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
    
    rcv_from.close();
    send_to.close();
    orchestrator_sub.close();
    return 0;
}
