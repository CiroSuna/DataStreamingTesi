#ifndef UTILS_H
#define UTILS_H
#include <zmq.hpp>
#include <cstring>

inline zmq::message_t zmq_str(const char* s) {
    return zmq::message_t(s, std::strlen(s));
}

bool sync_with_orchestrator(zmq::socket_t& sync_socket, const std::string& id);
bool cleanup_ipc_path(const std::string& ipc_path_file);
void check_zmq_recv(const zmq::recv_result_t& result, const std::string& error_msg);
    
#endif