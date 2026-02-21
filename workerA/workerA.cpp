#include <iostream>
#include <zmq.hpp>
#include "threadPool.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: workerA <ipc_filepath>\n";
        return 1;
    }
    
    std::string ipc_filepath = argv[1];
    
    std::cout << "WorkerA started with IPC: " << ipc_filepath << "\n";
    
    // TODO: Implementare logica workerA con threadPool
    
    return 0;
}
