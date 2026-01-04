#include <gtest/gtest.h>

#include "event.hpp"
#include "server/SingleThreadServer.hpp"   // Twoja klasa dziedzicząca po ServerCRTP

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

    // nie startujemy
    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::rejected_stopped);

    // metryki powinny to odzwierciedlać
    auto m = s.metrics();
    EXPECT_EQ(m.rejected_stopped_total, 1u);
}

TEST(SingleThreadServer, AcceptSubmitWhenRunning)
{
    SingleThreadServer s{16};
    ASSERT_TRUE(s.start());

    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::accepted);

    // accepted rośnie, niezależnie od tego czy event już został przetworzony
    auto m = s.metrics();
    EXPECT_EQ(m.accepted_total, 1u);

    s.shutdown(ShutdownMode::graceful);
    EXPECT_TRUE(s.wait());
}

TEST(SingleThreadServer, RejectWhenQueueFullDeterministic)
{
    // Klucz: capacity=0 daje w 100% deterministyczne rejected_full
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

    // Po shutdownie – kontraktowo odrzucamy
    auto st = s.trySubmit(Event{});
    EXPECT_EQ(st, SubmitStatus::rejected_stopped);

    s.wait();
}
