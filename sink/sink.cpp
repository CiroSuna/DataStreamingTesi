#include <iostream>
#include <zmq.hpp>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: sink <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath = argv[1];
    
    std::cout << "Sink started with IPC: " << ipc_filepath << "\n";
    
    // TODO: Implementare logica sink
    
    return 0;
}
