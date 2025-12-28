#pragma once

#include "MockOutputPolicy.hpp"
#include "publisher/publisher.hpp"
#include <sstream>
#include <string>

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
            
            using Pub = Publisher<test::MockPolicyTemplate, TextSink>;           
            Pub::publish(env, adapter);
        }
    };
}

