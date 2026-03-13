#ifndef LOGGER_H
#define LOGGER_H

#include <fstream>
#include <mutex>
#include <string>

class Logger {
    private:
        Logger() = default;
        std::ofstream log_file;
        std::mutex mtx;

        void write(const std::string& level, const std::string& caller_id, const std::string& msg);

    public:
        static Logger& instance();
        void init(const std::string& filename);
        void info(const std::string& caller_id, const std::string& msg);
        void debug(const std::string& caller_id, const std::string& msg);
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
};

#define LOG_INFO(who, msg)  Logger::instance().info(who, msg)
#define LOG_DEBUG(who, msg) Logger::instance().debug(who, msg)

#endif 