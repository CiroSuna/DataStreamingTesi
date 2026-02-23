#include <iostream>
#include <zmq.hpp>
#include "threadPool.hpp"
#include "dataTypes.hpp"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: workerA <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath = argv[1];
    std::string to_workerB_path = argv[2]; 


    zmq::context_t ctx {};
    zmq::socket_t rcv_from {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to {ctx, zmq::socket_type::push};

    send_to.bind(to_workerB_path);
    rcv_from.connect(ipc_filepath);
   
    zmq::message_t msg {sizeof(data)};
    rcv_from.recv(msg);
    data d{}; 
    std::memcpy(&d, msg.data(), sizeof(data));

    std::cout << "A: Dato ricevuto da sender: " << d.curr_value << '\n';;

   send_to.send(msg, zmq::send_flags::none);
    // TODO: Implementare logica workerA con threadPool
    
    rcv_from.close();
    send_to.close();
    return 0;
}
