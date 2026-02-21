#ifndef PIPE_PROCESSES_HPP
#define PIPE_PROCESSES_HPP 

#include <iostream>

int sender(std::string& ipc_filepath);
int workerA(std::string& ipc_filepath);
int workerB(const std::string& ipc_filepath);
int sink(std::string& ipc_filepath);

#endif