#pragma once
#include <string_view>
#include <utility>

#include "sinkPublisher.hpp"
#include "policyPublisher.hpp"

// Envelope       – any envelope type (RequestPayload, ResponsePayload, etc.)
// Sink           – a concrete sink, e.g. JsonSink, TextSink
// PolicyTemplate – a concrete policy template, e.g. TerminalPolicy, FilePolicy
//
// Usage:
// using Pub = Publisher<TerminalPolicy, JsonSink>;
// Pub::publish(env, &adapter_to_string_view);
//
// where adapter_to_string_view has the signature:
//    std::string_view adapter_to_string_view(const Envelope&);

template<
    template<typename> class PolicyTemplate,
    typename Sink
>
struct Publisher {
    template<typename Envelope>
    static void publish(
        const Envelope& env,
        std::string_view (*to_view)(const Envelope&)
    )
    {
        // 1) Convert Envelope -> std::string_view via the adapter
        std::string_view view = to_view(env);

        // 2) Instantiate Policy with Sink (at compile time),
        //    create a policy object and pass the string_view through it
        PolicyTemplate<Sink> policy{Sink{}};
        policy.publish(view);
    }
};


// ---------------------------------------------------------
// Explicit specializations for concrete combinations
// ---------------------------------------------------------

// TerminalPolicy + JsonSink
template<>
struct Publisher<TerminalPolicy, JsonSink> {
    template<typename Envelope>
    static void publish(
        const Envelope& env,
        std::string_view (*to_view)(const Envelope&)
    )
    {
        std::string_view view = to_view(env);

        TerminalPolicy<JsonSink> policy{};
        policy.publish(view);
    }
};

// TerminalPolicy + TextSink
template<>
struct Publisher<TerminalPolicy, TextSink> {
    template<typename Envelope>
    static void publish(
        const Envelope& env,
        std::string_view (*to_view)(const Envelope&)
    )
    {
        std::string_view view = to_view(env);

        TerminalPolicy<TextSink> policy{};
        policy.publish(view);
    }
};

// FilePolicy + JsonSink
template<>
struct Publisher<FilePolicy, JsonSink> {
    template<typename Envelope>
    static void publish(
        const Envelope& env,
        std::string_view (*to_view)(const Envelope&)
    )
    {
        std::string_view view = to_view(env);

        FilePolicy<JsonSink> policy{};   // tu zakładam default ctor
        policy.publish(view);
    }
};

// FilePolicy + TextSink
template<>
struct Publisher<FilePolicy, TextSink> {
    template<typename Envelope>
    static void publish(
        const Envelope& env,
        std::string_view (*to_view)(const Envelope&)
    )
    {
        std::string_view view = to_view(env);

        FilePolicy<TextSink> policy{};
        policy.publish(view);
    }
};