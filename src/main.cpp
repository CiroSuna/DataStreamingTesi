#include <iostream>
#include <zmq.hpp>
#include <unistd.h>
#include "threadPool.hpp"

void task(void){
    std::cout << "Task in eseguzione!";
}
int main(int argc, char const *argv[]){

    ThreadPool pool{5}; 
    pool.add_task(task);
    pool.shutdown();
    std::cout << "Inizio Tesi!\n";
    return 0;
}
