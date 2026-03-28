#include <gtest/gtest.h>
#include <vector>
#include <set>
#include "logger/core/lockfree_queue.hpp"

using logger::core::detail::FreeList;
using logger::core::detail::LogRecord;

TEST(FreeList, StartsEmpty) {
    FreeList fl;
    EXPECT_TRUE(fl.empty());
    EXPECT_EQ(fl.try_pop(), nullptr);
}

TEST(FreeList, PushPopSingle) {
    FreeList fl;
    LogRecord node{};
    fl.push(&node);
    EXPECT_FALSE(fl.empty());

    auto* popped = fl.try_pop();
    EXPECT_EQ(popped, &node);
    EXPECT_TRUE(fl.empty());
}

TEST(FreeList, LIFOOrdering) {
    FreeList fl;
    LogRecord a{}, b{}, c{};

    fl.push(&a);
    fl.push(&b);
    fl.push(&c);

    EXPECT_EQ(fl.try_pop(), &c);
    EXPECT_EQ(fl.try_pop(), &b);
    EXPECT_EQ(fl.try_pop(), &a);
    EXPECT_TRUE(fl.empty());
}

TEST(FreeList, PopFromEmptyReturnsNull) {
    FreeList fl;
    EXPECT_EQ(fl.try_pop(), nullptr);
    EXPECT_EQ(fl.try_pop(), nullptr);
}

TEST(FreeList, PushNPopNAllReturned) {
    constexpr int N = 64;
    FreeList fl;
    std::vector<LogRecord> nodes(N);

    for (auto& n : nodes) {
        fl.push(&n);
    }
    EXPECT_FALSE(fl.empty());

    std::set<LogRecord*> popped;
    for (int i = 0; i < N; ++i) {
        auto* p = fl.try_pop();
        ASSERT_NE(p, nullptr);
        popped.insert(p);
    }

    EXPECT_EQ(popped.size(), static_cast<size_t>(N));
    EXPECT_TRUE(fl.empty());
    EXPECT_EQ(fl.try_pop(), nullptr);
}
