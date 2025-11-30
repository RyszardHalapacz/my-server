#pragma once

#include <tuple>
#include <utility>
#include <string_view>

#include "common/messages/payloads/payloads.hpp"  // MsgTag, PayloadBase, Severity itd.

namespace logger::registry {

// Primary template – intentionally undefined.
template<MsgTag Tag>
struct PayloadRegister;


// ---------- Generic ----------

struct alignas(64) GenericPayload
    : PayloadBase<MsgTag::Generic, GenericPayload>
{
    void debug_impl() const {}
};

template<>
struct PayloadRegister<MsgTag::Generic> {
    using payload_type = GenericPayload;
    using base_type    = PayloadBase<MsgTag::Generic, GenericPayload>;

    // Typ argumentów: same typy z log_payloads.def
    using args_tuple_type = decltype(
        std::tuple{
        #define X(C,F) std::declval<C>(),
        #include "common/messages/payloads/log_payloads.def"
        #undef X
        }
    );

    // Wskaźniki do pól nagłówka – UWAGA: na bazę, nie na GenericPayload
    static constexpr auto field_ptrs = std::tuple{
        #define X(C,F) &base_type::F,
        #include "common/messages/payloads/log_payloads.def"
        #undef X
    };
};



// ---------- Request ----------

struct alignas(64) RequestPayload
    : PayloadBase<MsgTag::Request, RequestPayload>
{
    #define X(C,F) C F;
    #include "common/messages/payloads/log_requestpayload.def"
    #undef X

    void debug_impl() const {}
};

template<>
struct PayloadRegister<MsgTag::Request> {
    using payload_type = RequestPayload;
    using base_type    = PayloadBase<MsgTag::Request, RequestPayload>;

    // Header (.def) + request (.def) – typy argumentów
    using args_tuple_type = decltype(
        std::tuple{
        #define X(C,F) std::declval<C>(),
        #include "common/messages/payloads/log_payloads.def"
        #undef X
        #define X(C,F) std::declval<C>(),
        #include "common/messages/payloads/log_requestpayload.def"
        #undef X
        }
    );

    // Wskaźniki:
    //  - najpierw na pola z bazy (header),
    //  - potem na pola z RequestPayload (body).
    static constexpr auto field_ptrs = std::tuple{
        // header from PayloadBase
        #define X(C,F) &base_type::F,
        #include "common/messages/payloads/log_payloads.def"
        #undef X

        // request-specific fields from RequestPayload
        #define X(C,F) &payload_type::F,
        #include "common/messages/payloads/log_requestpayload.def"
        #undef X
    };
};


} // namespace logger::registry
