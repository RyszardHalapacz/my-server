#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "event.hpp"
#include "server/single_thread_server.hpp"

using namespace server;

TEST(SingleThreadServerExtended, StartIdempotency) {
    SingleThreadServer s{16};
    ASSERT_TRUE(s.start());
    EXPECT_TRUE(s.start());
    EXPECT_EQ(s.state(), ServerState::running);

    s.shutdown(ShutdownMode::graceful);
    s.wait();
}

TEST(SingleThreadServerExtended, ConcurrencyIsOne) {
    SingleThreadServer s{16};
    EXPECT_EQ(s.concurrency(), 1u);
}

TEST(SingleThreadServerExtended, ForceShutdownClearsQueue) {
    SingleThreadServer s{1024};
    ASSERT_TRUE(s.start());

    for (int i = 0; i < 100; ++i) {
        s.trySubmit(Event{});
    }

    s.shutdown(ShutdownMode::force);
    EXPECT_TRUE(s.wait());

    auto m = s.metrics();
    EXPECT_EQ(m.queue_depth_snapshot, 0u);
}

TEST(SingleThreadServerExtended, DoubleShutdownIsSafe) {
    SingleThreadServer s{16};
    ASSERT_TRUE(s.start());

    s.shutdown(ShutdownMode::graceful);
    s.shutdown(ShutdownMode::graceful);
    EXPECT_TRUE(s.wait());
    EXPECT_EQ(s.state(), ServerState::stopped);
}

TEST(SingleThreadServerExtended, MetricsQueueDepthReflectsQueueState) {
    SingleThreadServer s{16};
    auto m = s.metrics();
    EXPECT_EQ(m.queue_depth_snapshot, 0u);
}
