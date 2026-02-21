#include <iostream>
#include <zmq.hpp>
#include "threadPool.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: workerB <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath = argv[1];
    
    std::cout << "WorkerB started with IPC: " << ipc_filepath << "\n";
    
    // TODO: Implementare logica workerB con threadPool
    
    return 0;
}
