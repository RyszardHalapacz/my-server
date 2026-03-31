#pragma once

#include <cstdint>

namespace server {

enum class ServerState : uint8_t {
    created,
    running,
    stopping,
    stopped,
    failed
};

enum class ShutdownMode : uint8_t {
    graceful,
    force
};

enum class SubmitStatus : uint8_t {
    accepted,
    rejected_full,
    rejected_stopped,
    invalid,
    error
};

enum class ProtocolKind : uint8_t {
    mysql,
    tcp,
    http,
    custom
};

enum class EndpointKind : uint8_t {
    socket,
    file,
    terminal,
    custom
};

} // namespace server
