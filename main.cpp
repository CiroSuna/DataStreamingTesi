#include <iostream>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"

void process_starter(const std::string& process_name, const std::string& in_socket, const std::string& out_socket){
    pid_t new_proc {fork()};
    if (new_proc == -1) {
        perror(("Errore fork " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
    else if (new_proc == 0) {
        std::string path {"./" + process_name + "/" + process_name};
        execl(path.c_str(), process_name.c_str(), in_socket.c_str(), out_socket.c_str(), nullptr);
        perror(("Errore execl " + process_name).c_str());
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char const *argv[]){

    // Definizione endpoint ZeroMQ (pipeline lineare: sender → workerA → workerB → sink)
    const std::string sender_to_workerA = "ipc:///tmp/workA.ipc";
    const std::string workerA_to_workerB = "ipc:///tmp/workB.ipc";
    const std::string workerB_to_sink = "ipc:///tmp/sink.ipc"; 
    
    // Avvio processi
    process_starter("sender", "", sender_to_workerA);
    process_starter("workerA", sender_to_workerA, workerA_to_workerB);
    process_starter("workerB", workerA_to_workerB, workerB_to_sink);
    process_starter("sink", workerB_to_sink, "");

    return 0;
}
