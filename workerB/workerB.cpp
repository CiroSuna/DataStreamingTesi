#include <iostream>
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
    
    zmq::context_t ctx {};
    zmq::socket_t rcv_from_A {ctx, zmq::socket_type::pull};
    zmq::socket_t send_to_sink {ctx, zmq::socket_type::push};

    send_to_sink.bind(to_sink_path);
    rcv_from_A.connect(ipc_filepath);
   
    zmq::message_t msg {sizeof(data)};
    rcv_from_A.recv(msg);
    data d{}; 
    std::memcpy(&d, msg.data(), sizeof(data));

    std::cout << "B: Dato ricevuto da sender: " << d.curr_value << '\n';;

    send_to_sink.send(msg, zmq::send_flags::none); 
    
    // TODO: Implementare logica workerB con threadPool
    send_to_sink.close();
    rcv_from_A.close(); 
    return 0;
}
