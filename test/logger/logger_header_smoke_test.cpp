// test/logger/logger_header_smoke_test.cpp

#include <cstdint>
#include <gtest/gtest.h>

#include "common/messages/log_message.hpp"
#include "logger/Logger.hpp"

TEST(LoggerHeaderSmokeTest, AcceptsValidHeaderPacket)
{
    using logger::Handler;

    // Prepare a valid header packet in the same order as in log_payloads.def
    const Severity      severity       = Severity::Info;   // adjust if enum name differs
    const std::uint64_t timestamp      = 123456u;
    const std::uint32_t thread_id      = 7u;
    const std::uint32_t request_id     = 999u;
    const std::uint16_t class_id       = 1u;
    const std::uint16_t method_id      = 2u;
    const std::uint16_t schema_version = 1u;

    // This should compile and run without throwing or crashing:
    // Args... -> pack_header_args -> tuple -> Builder::build<Tag>(...)
    Handler::log<MsgTag::Generic>(
        severity,
        timestamp,
        thread_id,
        request_id,
        class_id,
        method_id,
        schema_version
    );

    // If we reached this point, the basic logging pipeline is at least wired correctly.
    SUCCEED();
}
