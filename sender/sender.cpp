#include <iostream>
#include <cstdio>
#include <zmq.hpp>
#include <unistd.h>
#include "dataTypes.hpp"




int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << ("Arguments not well formed");
        return 1;
    }
     
    
    std::string ipc_filepath {argv[2]};
    zmq::context_t ctx {};
    zmq::socket_t send_to {ctx, zmq::socket_type::push};
    zmq::socket_t orchestrator {ctx, zmq::socket_type::sub};
    
    // Bind to push socket for workerA
    std::remove(ipc_filepath.c_str());
    send_to.bind(ipc_filepath);
    orchestrator.connect(argv[3]); 
    
    sleep(1); 
    data d {10}; 
    zmq::message_t msg {sizeof(data)};
    std::memcpy(msg.data(), &d, sizeof(data));
    sleep(1);
    send_to.send(msg, zmq::send_flags::none);
    std::cout << "dato inviato a workerA: " << d.curr_value << '\n'; 
    
    
    send_to.close();
    orchestrator.close();
    return 0;
}
