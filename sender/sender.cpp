#include <iostream>
#include <zmq.hpp>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: sender <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath {argv[1]};
    
    std::cout << "Sender started with IPC: " << ipc_filepath << "\n";
    
    // TODO: Implementare logica sender
    
    return 0;
}
