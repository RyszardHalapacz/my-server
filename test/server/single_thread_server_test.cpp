#include <gtest/gtest.h>

#include "event.hpp"
#include "server/single_thread_server.hpp"  // class inheriting from ServerCRTP

using namespace server;

TEST(SingleThreadServer, StartStopLifecycle)
{
    SingleThreadServer s{16};

    EXPECT_EQ(s.state(), ServerState::created);

    EXPECT_TRUE(s.start());
    EXPECT_EQ(s.state(), ServerState::running);

    s.shutdown(ShutdownMode::graceful);
    EXPECT_TRUE(s.wait());
    EXPECT_EQ(s.state(), ServerState::stopped);
}

TEST(SingleThreadServer, RejectSubmitWhenNotRunning)
{
    SingleThreadServer s{16};

    // server not started
    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::rejected_stopped);

    // metrics should reflect this
    auto m = s.metrics();
    EXPECT_EQ(m.rejected_stopped_total, 1u);
}

TEST(SingleThreadServer, AcceptSubmitWhenRunning)
{
    SingleThreadServer s{16};
    ASSERT_TRUE(s.start());

    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::accepted);

    // accepted increments regardless of whether the event has been processed yet
    auto m = s.metrics();
    EXPECT_EQ(m.accepted_total, 1u);

    s.shutdown(ShutdownMode::graceful);
    EXPECT_TRUE(s.wait());
}

TEST(SingleThreadServer, RejectWhenQueueFullDeterministic)
{
    // Key: capacity=0 gives 100% deterministic rejected_full
    SingleThreadServer s{0};
    ASSERT_TRUE(s.start());

    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::rejected_full);

    auto m = s.metrics();
    EXPECT_EQ(m.rejected_full_total, 1u);

    s.shutdown(ShutdownMode::force);
    EXPECT_TRUE(s.wait());
}

TEST(SingleThreadServer, RejectAfterShutdown)
{
    SingleThreadServer s{16};
    ASSERT_TRUE(s.start());

    s.shutdown(ShutdownMode::graceful);

    // After shutdown — contractually we reject
    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::rejected_stopped);

    s.wait();
}
