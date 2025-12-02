#pragma once
#include "common/messages/log_message.hpp"
#include "builder.hpp"
#include "header_args.hpp"

namespace logger {

struct Handler {
    template<MsgTag Tag, typename... Args>
    static void log(Args&&... args) {
        // 1) Args -> tuple
        auto header_tuple =
            registry::pack_header_args(std::forward<Args>(args)...);

        // 2) tuple -> payload
        auto payload = registry::Builder::build<Tag>(header_tuple);

        // 3) payload -> packet -> MPSC
        // auto packet = make_log_packet(std::move(payload));
        // core().enqueue(std::move(packet));
    }

private:
    //static LogCore& core(); // dostęp do wspólnej kolejki / rdzenia
};

} // namespace logger
