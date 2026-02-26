#include "utils.hpp"
#include "dataTypes.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <filesystem>
#include <cerrno>


bool cleanup_ipc_path(const std::string& ipc_path_file) {
    std::filesystem::path p(ipc_path_file);
    if (!p.empty() && std::filesystem::exists(p)) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
        if (ec) {
            std::cerr << "Failed to remove IPC file " << ipc_path_file
                      << ": " << ec.message() << '\n';
            return false;
        }
    }
    return true;
}

bool sync_with_orchestrator(zmq::socket_t& sync_socket, const std::string& id) {
    // REQ/REP barrier: send READY, wait for GO.
    std::cout << id << ": Sending READY to orchestrator...\n" << std::flush;
    auto send_result = sync_socket.send(zmq::message_t("READY", 5), zmq::send_flags::none);
    if (!send_result.has_value()) {
        std::cerr << id << ": Error sending READY\n";
        return false;
    }
    std::cout << id << ": READY sent, waiting for GO...\n" << std::flush;

    zmq::message_t res {};
    auto check_res = sync_socket.recv(res, zmq::recv_flags::none);
    if (!check_res.has_value()) {
        std::cerr << id << ": Error, no response from orchestrator\n";
        return false;
    }
    std::string r {static_cast<char*>(res.data()), res.size()};
    if (r != "GO") {
        std::cerr << id << ": Error, unexpected response from orchestrator: " << r << '\n';
        return false;
    }
    std::cout << id << ": GO received, proceeding\n" << std::flush;
    return true;
}