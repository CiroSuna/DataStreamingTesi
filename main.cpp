#include <iostream>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"

void process_starter(const std::string& process_name, const std::string& in_socket, const std::string& out_socket, const std::string& orchestrator_ipc){
    pid_t new_proc {fork()};
    if (new_proc == -1) {
        perror(("Errore fork " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
    else if (new_proc == 0) {
        std::string path {"./" + process_name + "/" + process_name};
        execl(path.c_str(), process_name.c_str(), in_socket.c_str(), out_socket.c_str(), orchestrator_ipc.c_str(), nullptr);
        perror(("Errore execl " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char const *argv[]){

    // TODO: Scegliere come gestire il passaggio dei socket tra i vari processi (file configurazione, passarli con exec, o hardcoded)
    std::string orchestrator_ipc_path {"ipc:///tmp/orchestrator.ipc"};
    zmq::context_t ctx{};
    zmq::socket_t orchestrator {ctx, zmq::socket_type::pub};

    // Zmq endpoints and socket names definitions
    const std::string sender_to_workerA {"ipc:///tmp/workA.ipc"};
    const std::string workerA_to_workerB {"ipc:///tmp/workb.ipc"};
    const std::string workerb_to_sink {"ipc:///tmp/sink.ipc"}; 
    
    // starting processes 
    process_starter("sender", "", sender_to_workerA, orchestrator_ipc_path);
    process_starter("workera", sender_to_workerA, workerA_to_workerB, orchestrator_ipc_path);
    process_starter("workerb", workerA_to_workerB, workerb_to_sink, orchestrator_ipc_path);
    process_starter("sink", workerb_to_sink, "", orchestrator_ipc_path);

    orchestrator.close();
    return 0;
}
