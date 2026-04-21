#include <gtest/gtest.h>
#include <tuple>
#include <type_traits>
#include <string_view>

#include "common/messages/log_message.hpp"
#include "logger/registry/payload_register.hpp"

using logger::registry::PayloadRegister;
using logger::registry::GenericPayload;
using logger::registry::RequestPayload;

template<typename T>
using decay_t = std::decay_t<T>;

TEST(PayloadRegister, Generic_PayloadTypeMatches) {
    using Reg = PayloadRegister<MsgTag::Generic>;
    static_assert(std::is_same_v<typename Reg::payload_type, GenericPayload>);
    SUCCEED();
}

TEST(PayloadRegister, Request_PayloadTypeMatches) {
    using Reg = PayloadRegister<MsgTag::Request>;
    static_assert(std::is_same_v<typename Reg::payload_type, RequestPayload>);
    SUCCEED();
}

TEST(PayloadRegister, Generic_ArgsTupleSizeMatchesFieldPtrsSize) {
    using Reg = PayloadRegister<MsgTag::Generic>;
    using Args = typename Reg::args_tuple_type;
    using FieldPtrsTuple = decay_t<decltype(Reg::field_ptrs)>;

    static_assert(std::tuple_size_v<Args> == std::tuple_size_v<FieldPtrsTuple>);
    SUCCEED();
}

TEST(PayloadRegister, Request_ArgsTupleSizeMatchesFieldPtrsSize) {
    using Reg = PayloadRegister<MsgTag::Request>;
    using Args = typename Reg::args_tuple_type;
    using FieldPtrsTuple = decay_t<decltype(Reg::field_ptrs)>;

    static_assert(std::tuple_size_v<Args> == std::tuple_size_v<FieldPtrsTuple>);
    SUCCEED();
}
