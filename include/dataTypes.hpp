#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <string>

// IPC Socket paths
namespace ipc_paths {
    constexpr const char* ORCHESTRATOR = "/tmp/orchestrator.ipc";
    constexpr const char* SENDER_TO_WORKERA = "/tmp/workA.ipc";
    constexpr const char* WORKERA_TO_WORKERB = "/tmp/workb.ipc";
    constexpr const char* WORKERB_TO_SINK = "/tmp/sink.ipc";
    constexpr const char* SYNC_SOCK = "/tmp/sync.ipc";
    
    inline std::string orchestrator() { return "ipc:///tmp/orchestrator.ipc"; }
    inline std::string sender_to_workerA() { return "ipc:///tmp/workA.ipc"; }
    inline std::string workerA_to_workerB() { return "ipc:///tmp/workb.ipc"; }
    inline std::string workerB_to_sink() { return "ipc:///tmp/sink.ipc"; }
    inline std::string sync_socket_path() { return "ipc:///tmp/sync.ipc"; }
}

// Topic prefixes for pub-sub
namespace topics {
    constexpr const char* WORKERA = "[WorkA]";
    constexpr const char* WORKERB = "[WorkB]";
    constexpr const char* SYNC = "[Sync]";
    constexpr const char* GLOBAL = "[Global]";

    inline std::string workera_topic() { return WORKERA; }
    inline std::string workerb_topic() { return WORKERB; }
    inline std::string sync_topic() { return SYNC; }
    inline std::string global_topic() { return GLOBAL; }
}

enum update_type{
    THREAD_INC,
    THREAD_DEC
};

struct data{
    int curr_value{};
    int original_value{};
    data(int _x) : curr_value{_x}, original_value{_x} {}
    data(int _x, int _original_x) : curr_value{_x}, original_value{_original_x} {}
    data() : curr_value{}, original_value{} {}
    data(const data& d) : curr_value {d.curr_value}, original_value {d.original_value} {}
};

struct update_ms {
    update_type t {};
    int resize {};
    update_ms(update_type _t, int _resize) : t{_t}, resize{_resize} {}
    update_ms() : t{}, resize{} {}
};

#endif