#include <gtest/gtest.h>
#include <vector>
#include "logger/core/lockfree_queue.hpp"

using logger::core::detail::MpscQueue;
using logger::core::detail::LogRecord;

TEST(MpscQueue, StartsEmpty) {
    MpscQueue q;
    EXPECT_TRUE(q.empty());
}

TEST(MpscQueue, PushPopSingle) {
    MpscQueue q;
    LogRecord node{};
    q.push(&node);
    EXPECT_FALSE(q.empty());

    auto* popped = static_cast<LogRecord*>(q.pop());
    EXPECT_EQ(popped, &node);
    EXPECT_TRUE(q.empty());
}

TEST(MpscQueue, FIFOOrdering) {
    MpscQueue q;
    LogRecord a{}, b{}, c{};

    q.push(&a);
    q.push(&b);
    q.push(&c);

    EXPECT_EQ(static_cast<LogRecord*>(q.pop()), &a);
    EXPECT_EQ(static_cast<LogRecord*>(q.pop()), &b);
    EXPECT_EQ(static_cast<LogRecord*>(q.pop()), &c);
    EXPECT_TRUE(q.empty());
}

TEST(MpscQueue, PushNPopNOrdered) {
    constexpr int N = 64;
    MpscQueue q;
    std::vector<LogRecord> nodes(N);

    for (auto& n : nodes) {
        q.push(&n);
    }
    EXPECT_FALSE(q.empty());

    for (int i = 0; i < N; ++i) {
        auto* p = static_cast<LogRecord*>(q.pop());
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p, &nodes[i]);
    }

    EXPECT_TRUE(q.empty());
}
