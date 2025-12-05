#include <string_view>
#include <utility>

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
