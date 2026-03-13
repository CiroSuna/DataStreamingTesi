#include <iostream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include "logger.hpp"

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& filename) {
    log_file.open(filename, std::ios::trunc);
}

void Logger::write(const std::string& level, const std::string& caller_id, const std::string& msg) {
    auto t {std::time(nullptr)};

    std::unique_lock<std::mutex> lock(mtx);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%H:%M:%S")
        << " [" << level << "][" << caller_id << "] " << msg;
    std::string line {oss.str() + '\n'};
    if (log_file.is_open()) {
        log_file << line;
        log_file.flush();
    }
}

void Logger::info(const std::string& caller_id, const std::string& msg) {
    write("INFO", caller_id, msg);
}

void Logger::debug(const std::string& caller_id, const std::string& msg) {
    #ifdef DEBUG_BUILD
    write("DEBUG", caller_id, msg);
    #endif
}

