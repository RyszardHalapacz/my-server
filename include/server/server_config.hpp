#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/server_types.hpp"

namespace server {

struct ListenerConfig {
    uint16_t port = 0;
    ProtocolKind protocol = ProtocolKind::custom;
    bool enabled = true;
};

struct ParserConfig {
    bool enable_sql_dialect = true;
    bool allow_partial_parse = false;
    uint32_t max_tokens = 0;
    uint32_t max_input_size = 0;
};

struct ExecutionConfig {
    uint32_t worker_count = 1;
    bool enable_backpressure = true;
    uint64_t queue_capacity = 1024;
};

struct PublisherConfig {
    bool enabled = true;
    bool publish_raw_event = false;
    bool publish_parsed_input = false;
    bool publish_command = false;
    bool publish_result = false;
};

struct DistributorConfig {
    bool enabled = true;
};

struct EndpointConfig {
    EndpointKind kind = EndpointKind::custom;
    bool enabled = true;
    std::string name{};
};

struct LoggerConfig {
    bool enabled = true;
    bool log_submit = false;
    bool log_parse = false;
    bool log_execute = false;
    bool log_publish = false;
    bool log_errors = true;
};

struct ServerConfig {
    std::vector<ListenerConfig> listeners{};
    std::vector<EndpointConfig> endpoints{};

    ParserConfig parser{};
    ExecutionConfig execution{};
    PublisherConfig publisher{};
    DistributorConfig distributor{};
    LoggerConfig logger{};
};

} // namespace server
