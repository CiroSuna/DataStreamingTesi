#ifndef UTILS_H
#define UTILS_H
#include <zmq.hpp>


bool sync_with_orchestrator(zmq::socket_t& sync_socket, const std::string& id);
bool cleanup_ipc_path(const std::string& ipc_path_file);

#endif