#include <gtest/gtest.h>
#include "../test/logger/mocks/MockLogEngine.hpp"
#include "logger/Logger.hpp"

inline std::uint64_t now_ticks_us() noexcept
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

inline std::uint32_t mapped_thread_id() noexcept
{
    return static_cast<std::uint32_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

TEST(Pipeline, LogsToMockPolicy)
{

    using MockPolicyTextSink = logger::test::MockPolicyTemplate<TextSink>;
    using logger::core::detail::TestLogEngine;

    MockPolicyTextSink::clear();

    logger::Handler::log<MsgTag::Generic, TestLogEngine>(
        Severity::Info,
        now_ticks_us(),
        mapped_thread_id(),
        uint32_t{0},
        uint16_t{1},
        uint16_t{4},
        uint16_t{1});

    auto &logs = MockPolicyTextSink::get_output();
    ASSERT_EQ(logs.size(), 1);

    EXPECT_NE(logs[0].find("severity=Info"), std::string::npos);
    EXPECT_NE(logs[0].find("class_id=Handler"), std::string::npos); // TextSink formatuje!
}