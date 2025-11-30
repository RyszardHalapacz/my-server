#include <gtest/gtest.h>
#include <tuple>
#include <type_traits>
#include <string_view>

#include "common/messages/log_message.hpp"
#include "logger/registry/payload_register.hpp"
#include "logger/registry/builder.hpp"
// If Builder is in a separate header, include it here:
 // #include "logger/registry/builder.hpp"

using logger::registry::PayloadRegister;
using logger::registry::GenericPayload;
using logger::registry::RequestPayload;

template<typename T>
using decay_t = std::decay_t<T>;

// -----------------------
// Generic payload + Builder
// -----------------------

TEST(Builder, Generic_BuildsPayloadFromTuple_UsingDefs) {
    using Reg  = PayloadRegister<MsgTag::Generic>;
    using Args = typename Reg::args_tuple_type;
    using P    = typename Reg::payload_type;

    // 1) Prepare a source payload with known values.
    P src{};
    src.severity       = Severity::Warn;
    src.timestamp      = std::uint64_t{123};
    src.thread_id      = std::uint32_t{42};
    src.request_id     = std::uint32_t{7};
    src.class_id       = std::uint16_t{10};
    src.method_id      = std::uint16_t{20};
    src.schema_version = std::uint16_t{1};

    // 2) Build Args tuple using the same .def that defines header fields.
    //    Order is driven entirely by log_payloads.def.
    Args args{
    #define X(C,F) src.F,
    #include "common/messages/payloads/log_payloads.def"
    #undef X
    };

    // 3) Build the payload via Builder.
    auto payload = logger::registry::Builder::build<MsgTag::Generic>(args);

    static_assert(std::is_same_v<decltype(payload), P>,
                  "Builder must return the correct payload type for Generic");

    // 4) Compare all fields using the same .def file – no manual field listing here.
    #define X(C,F) EXPECT_EQ(payload.F, src.F);
    #include "common/messages/payloads/log_payloads.def"
    #undef X
}

// -----------------------
// Request payload + Builder
// -----------------------

TEST(Builder, Request_BuildsPayloadFromTuple_UsingDefs) {
    using Reg  = PayloadRegister<MsgTag::Request>;
    using Args = typename Reg::args_tuple_type;
    using P    = typename Reg::payload_type;

    using std::literals::string_view_literals::operator""sv;

    // 1) Prepare a source RequestPayload with both header and body fields.
    P src{};

    // Header fields from log_payloads.def
    src.severity       = Severity::Error;
    src.timestamp      = std::uint64_t{555};
    src.thread_id      = std::uint32_t{99};
    src.request_id     = std::uint32_t{1234};
    src.class_id       = std::uint16_t{3};
    src.method_id      = std::uint16_t{7};
    src.schema_version = std::uint16_t{2};

    // Request-specific fields from log_requestpayload.def
    src.req_unique_id  = std::uint64_t{987654321};
    src.path           = "/api/v1/users"sv;

    // 2) Build Args tuple using both header and body .def files.
    //    Order is guaranteed to match PayloadRegister<MsgTag::Request>.
    Args args{
        // Header (same order as in log_payloads.def)
        #define X(C,F) src.F,
        #include "common/messages/payloads/log_payloads.def"
        #undef X

        // Request body (same order as in log_requestpayload.def)
        #define X(C,F) src.F,
        #include "common/messages/payloads/log_requestpayload.def"
        #undef X
    };

    // 3) Build the payload via Builder.
    auto payload = logger::registry::Builder::build<MsgTag::Request>(args);

    static_assert(std::is_same_v<decltype(payload), P>,
                  "Builder must return the correct payload type for Request");

    // 4) Compare header fields.
    #define X(C,F) EXPECT_EQ(payload.F, src.F);
    #include "common/messages/payloads/log_payloads.def"
    #undef X

    // 5) Compare request-specific fields.
    #define X(C,F) EXPECT_EQ(payload.F, src.F);
    #include "common/messages/payloads/log_requestpayload.def"
    #undef X
}

// -----------------------
// Contract tests: args_tuple_type vs field_ptrs
// -----------------------

TEST(Builder, Generic_ArgsTupleAndFieldPtrsStayInSync) {
    using Reg  = PayloadRegister<MsgTag::Generic>;
    using Args = typename Reg::args_tuple_type;
    using FieldPtrsTuple = decay_t<decltype(Reg::field_ptrs)>;

    static_assert(std::tuple_size_v<Args> == std::tuple_size_v<FieldPtrsTuple>,
                  "Generic: args_tuple_type size must match field_ptrs size");
    SUCCEED();
}

TEST(Builder, Request_ArgsTupleAndFieldPtrsStayInSync) {
    using Reg  = PayloadRegister<MsgTag::Request>;
    using Args = typename Reg::args_tuple_type;
    using FieldPtrsTuple = decay_t<decltype(Reg::field_ptrs)>;

    static_assert(std::tuple_size_v<Args> == std::tuple_size_v<FieldPtrsTuple>,
                  "Request: args_tuple_type size must match field_ptrs size");
    SUCCEED();
}
