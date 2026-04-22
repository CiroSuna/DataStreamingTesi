#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <string>
#include <cstdint>

// IPC Socket paths
namespace ipc_paths {
    constexpr const char* ORCHESTRATOR = "/tmp/orchestrator.ipc";
    constexpr const char* SENDER_TO_WORKERA = "/tmp/workA.ipc";
    constexpr const char* WORKERA_TO_WORKERB = "/tmp/workb.ipc";
    constexpr const char* WORKERB_TO_SINK = "/tmp/sink.ipc";
    constexpr const char* SYNC_SOCK = "/tmp/sync.ipc";
    constexpr const char* ROUTER_SOCK = "/tmp/router.ipc"; 
    
    
    // TODO: maybe remove ?
    inline std::string orchestrator() { return "ipc:///tmp/orchestrator.ipc"; }
    inline std::string sender_to_workerA() { return "ipc:///tmp/workA.ipc"; }
    inline std::string workerA_to_workerB() { return "ipc:///tmp/workb.ipc"; }
    inline std::string workerB_to_sink() { return "ipc:///tmp/sink.ipc"; }
    inline std::string sync_socket_path() { return "ipc:///tmp/sync.ipc"; }
    inline std::string router_path() { return "ipc:///tmp/router.ipc"; }
}

// Topic prefixes for pub-sub
namespace topics {
    constexpr const char* WORKERA = "[WorkA]";
    constexpr const char* WORKERB = "[WorkB]";
    constexpr const char* SYNC = "[Sync]";
    constexpr const char* SENDER = "[Sender]";
    constexpr const char* SINK   = "[Sink]";
    constexpr const char* GLOBAL = "[Global]";

    inline std::string workera_topic() { return WORKERA; }
    inline std::string workerb_topic() { return WORKERB; }
    inline std::string sync_topic() { return SYNC; }
    inline std::string global_topic() { return GLOBAL; }
}

// Tagged message types for router payloads — add new tags here as needed
namespace msg_types {
    constexpr const char* THREAD_INC = "[THREAD_INC]";
    constexpr const char* THREAD_DEC = "[THREAD_DEC]";
    constexpr const char* LAMBDA_UPDATE = "[LAMBDA_UPDATE]";
    constexpr const char* ITEM_LATENCY = "[ITEM_LATENCY]";
    constexpr const char* RATE_UPDATE = "[RATE_UPDATE]";
}

// Sync/control messages
namespace messages {
    constexpr const char* READY = "READY";
    constexpr const char* GO = "GO";
    constexpr const char* SHUTDOWN = "SHUTDOWN";
    constexpr const char* END = "END";
    constexpr const char* OK = "OK";
}

// Identifies which process sent a message on the router socket (routing_id == topic string)
enum class ProcessId {
    WORKERA,
    WORKERB,
    SENDER,
    SINK,
    UNKNOWN
};


inline ProcessId parse_process_id(const std::string& id) {
    if (id == topics::WORKERA) return ProcessId::WORKERA;
    if (id == topics::WORKERB) return ProcessId::WORKERB;
    if (id == topics::SENDER)  return ProcessId::SENDER;
    if (id == topics::SINK)    return ProcessId::SINK;
    return ProcessId::UNKNOWN;
}

enum update_type{
    THREAD_INC,
    THREAD_DEC
};

struct data{
    int curr_value{};
    int original_value{};
    int64_t send_time{}; // ns: set by sender before sending
    int64_t workerA_time{}; // ns: set by workerA after processing, before forwarding to workerB
    int64_t workerB_time{}; // ns: set by workerB after processing, before forwarding to sink
    int64_t workerA_service_time{}; // ns: pure service time in workerA (no queue wait)
    int64_t workerB_service_time{}; // ns: pure service time in workerB (no queue wait)
    data(int _x) : curr_value{_x}, original_value{_x}, send_time{}, workerA_time{}, workerB_time{}, workerA_service_time{}, workerB_service_time{} {}
    data(int _x, int _original_x) : curr_value{_x}, original_value{_original_x}, send_time{}, workerA_time{}, workerB_time{}, workerA_service_time{}, workerB_service_time{} {}
    data() : curr_value{}, original_value{}, send_time{}, workerA_time{}, workerB_time{}, workerA_service_time{}, workerB_service_time{} {}
    data(const data& d) : curr_value{d.curr_value}, original_value{d.original_value}, send_time{d.send_time}, workerA_time{d.workerA_time}, workerB_time{d.workerB_time}, workerA_service_time{d.workerA_service_time}, workerB_service_time{d.workerB_service_time} {}
};

struct update_ms {
    update_type t {};
    int resize {};
    update_ms(update_type _t, int _resize) : t{_t}, resize{_resize} {}
    update_ms() : t{}, resize{} {}
};

// Per-item pipeline latency, sent by sink to orchestrator
struct item_latency {
    double sender_to_A{}; // seconds: sender → workerA (includes queue wait)
    double A_to_B{}; // seconds: workerA → workerB (includes queue wait)
    double B_to_sink{}; // seconds: workerB → sink
    double end_to_end{}; // seconds: sender → sink
    double service_time_A{}; // seconds: pure service time in workerA
    double service_time_B{}; // seconds: pure service time in workerB
    int64_t send_time{}; // ns: original send_time from sender (for lambda estimation)
};

#endif