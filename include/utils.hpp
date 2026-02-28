#ifndef UTILS_H
#define UTILS_H
#include <zmq.hpp>


bool sync_with_orchestrator(zmq::socket_t& sync_socket, const std::string& id);
bool cleanup_ipc_path(const std::string& ipc_path_file);
void check_zmq_recv(const zmq::recv_result_t& result, const std::string& error_msg);
void check_zmq_recv(const zmq::recv_result_t& result, const std::string& error_msg);
    
#endif