#pragma once

#include <tuple>
#include <utility>
#include <string_view>

#include "common/messages/payloads/payloads.hpp"  // MsgTag, PayloadBase, Severity, etc.

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

    // Argument types – exactly the types from log_payloads.def (header only).
    using args_tuple_type = decltype(
        std::tuple{
        #define X(C,F) std::declval<C>(),
        #include "common/messages/payloads/log_payloads.def"
        #undef X
        }
    );

    // Pointers to header fields – note: they point to the base, not GenericPayload.
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

    // Argument types: header (.def) + request body (.def).
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

    // Field pointers:
    //  - first header fields from the base,
    //  - then request-specific fields from RequestPayload.
    static constexpr auto field_ptrs = std::tuple{
        // header from PayloadBase
        #define X(C,F) &base_type::F,
        #include "common/messages/payloads/log_payloads.def"
        #undef X

        // request-specific fields
        #define X(C,F) &payload_type::F,
        #include "common/messages/payloads/log_requestpayload.def"
        #undef X
    };
};

} // namespace logger::registry
