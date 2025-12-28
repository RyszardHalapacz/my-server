#pragma once
#include "common/messages/log_message.hpp"
#include "builder.hpp"
#include "header_args.hpp"
#include "publisher.hpp"
#include "core/logEngine.hpp"

namespace logger
{

    struct Handler
    {
        template <MsgTag Tag, 
        typename Engine = core::detail::LogEngine,
        typename... Args>
        static void log(Args &&...args)
        {

            // 1) Args -> tuple with header fields
            auto header_tuple =
                registry::pack_header_args(std::forward<Args>(args)...);

            // 2) tuple -> payload (constructed by the registry::Builder)
            auto payload = registry::Builder::build<Tag>(header_tuple);

            // 3) payload -> packet -> MPSC queue
            // auto packet = make_log_packet(std::move(payload));
            // core().enqueue(std::move(packet));
             auto& engine = Engine::instance();
            engine.enqueue(std::move(payload));
        }

    private:
        // static LogCore& core(); // access to the shared logging core and its queue
    };

    template <
        template <typename> class PolicyTemplate,
        typename Sink>
    struct LogCore
    {
        template <typename Envelope>
        static void submit(
            const Envelope &env,
            std::string_view (*to_view)(const Envelope &))
        {
            // Core only selects the template combination
            // and delegates to Publisher – ZERO data members here.
         
            Publisher<PolicyTemplate, Sink>::publish(env, to_view);
        }
    };

} // namespace logger
