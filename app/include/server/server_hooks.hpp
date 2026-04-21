#pragma once

#include <string_view>

#include "event.hpp"
#include "server/server_types.hpp"

namespace server {

struct ServerHookTable {
    using EventHook  = void(*)(const ::Event&) noexcept;
    using TextHook   = void(*)(std::string_view) noexcept;
    using StatusHook = void(*)(SubmitStatus) noexcept;

    EventHook  on_event_received = nullptr;
    StatusHook on_submit_result  = nullptr;

    TextHook on_raw_input        = nullptr;
    TextHook on_parsed_input     = nullptr;
    TextHook on_command_built    = nullptr;
    TextHook on_result_ready     = nullptr;
    TextHook on_distributed      = nullptr;
    TextHook on_error            = nullptr;
};

namespace hooks {

void noopEvent(const ::Event&) noexcept;
void noopText(std::string_view) noexcept;
void noopStatus(SubmitStatus) noexcept;

ServerHookTable makeNoopHooks() noexcept;

} // namespace hooks
} // namespace server
