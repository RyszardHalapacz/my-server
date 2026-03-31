#include "server/server_hooks.hpp"

namespace server::hooks {

void noopEvent(const ::Event&) noexcept {}
void noopText(std::string_view) noexcept {}
void noopStatus(SubmitStatus) noexcept {}

ServerHookTable makeNoopHooks() noexcept {
    ServerHookTable h;
    h.on_event_received = &noopEvent;
    h.on_submit_result  = &noopStatus;
    h.on_raw_input      = &noopText;
    h.on_parsed_input   = &noopText;
    h.on_command_built  = &noopText;
    h.on_result_ready   = &noopText;
    h.on_distributed    = &noopText;
    h.on_error          = &noopText;
    return h;
}

} // namespace server::hooks
