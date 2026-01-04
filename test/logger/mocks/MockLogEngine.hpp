#pragma once

#include "MockOutputPolicy.hpp"
#include "publisher/publisher.hpp"
#include <sstream>
#include <string>

template<>
struct Publisher<::logger::test::MockPolicyTemplate, TextSink> {

    template<typename Envelope>
    static void publish(
        const Envelope& env,
        std::string_view (*to_view)(const Envelope&)
    )
    {
        std::string_view view = to_view(env);
        ::logger::test::MockPolicyTemplate<TextSink> policy{};
        policy.publish(view);
    }
};

namespace logger::core::detail
{
    class TestLogEngine
    {
    public:
        static TestLogEngine &instance() noexcept
        {
            static TestLogEngine eng;
            return eng;
        }

        template <typename Envelope>
        void enqueue(Envelope&& env)
        {
            using EnvClean = std::decay_t<Envelope>;
            
            auto adapter = +[](const EnvClean& envelope) -> std::string_view {
                thread_local std::string buffer;
                buffer.clear();
                
                std::ostringstream oss;
                envelope.debug_print(oss);
                buffer = oss.str();
                
                return std::string_view{buffer};
            };
            
            using Pub = Publisher<::logger::test::MockPolicyTemplate, TextSink>;           
            Pub::publish(env, adapter);
        }
    };
}