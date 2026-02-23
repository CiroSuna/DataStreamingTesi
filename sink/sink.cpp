#include <iostream>
#include <zmq.hpp>
#include "dataTypes.hpp"


int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: sink <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath {argv[1]};
    zmq::context_t ctx {};
    zmq::socket_t rcv_from_B {ctx, zmq::socket_type::pull}; 
    
    rcv_from_B.connect(ipc_filepath);
        

    zmq::message_t msg {sizeof(data)};
    rcv_from_B.recv(msg);
    data d{}; 
    std::memcpy(&d, msg.data(), sizeof(data));

    std::cout << "Sink: Dato ricevuto da B: " << d.curr_value << '\n';

    
    // TODO: Implementare logica sink
    
    rcv_from_B.close();
    return 0;
}
