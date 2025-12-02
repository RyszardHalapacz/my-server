// This file is a *compile-fail* test on purpose.
// It demonstrates that Logger::Handler::log<Tag>(...) rejects wrong header types.
//
// To verify it, temporarily remove the #if 0 / #endif guards
// and try to build the project. Compilation *must* fail with
// "Builder: argument type does not exactly match payload field type".

#if 0

#include <cstdint>
#include <gtest/gtest.h>

#include "common/messages/log_message.hpp"
#include "logger/Logger.hpp"

TEST(LoggerHeaderNegativeTest, RejectsWrongHeaderTypes)
{
    using logger::Handler;

    // Correct layout from log_payloads.def is:
    // Severity,
    // std::uint64_t  timestamp,
    // std::uint32_t  thread_id,
    // std::uint32_t  request_id,
    // std::uint16_t  class_id,
    // std::uint16_t  method_id,
    // std::uint16_t  schema_version

    const Severity      severity       = Severity::Info;
    const std::uint32_t timestamp      = 123456u;  // WRONG: should be std::uint64_t
    const std::uint32_t thread_id      = 7u;
    const std::uint32_t request_id     = 999u;
    const std::uint16_t class_id       = 1u;
    const std::uint16_t method_id      = 2u;
    const std::uint16_t schema_version = 1u;

    // This call *must not* compile if Builder's static_asserts work correctly.
    Handler::log<MsgTag::Generic>(
        severity,
        timestamp,      // wrong type
        thread_id,
        request_id,
        class_id,
        method_id,
        schema_version
    );

    // If this ever compiles, the type-safety contract is broken.
    SUCCEED();
}

#endif // end of intentional compile-fail test
