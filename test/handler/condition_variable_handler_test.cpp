#include <gtest/gtest.h>
#include <vector>
#include <mutex>
#include <condition_variable>
#include "handler/condition_variable_database_handler.hpp"

using global::DatabaseConnection::status;

TEST(ConditionVariableHandler, ConstructAndDestroy) {
    std::vector<Event> events;
    std::mutex mtx;
    std::condition_variable cv;
    {
        ConditionVariableDatabaseHandler h{events, mtx, cv};
    }
    SUCCEED();
}

TEST(ConditionVariableHandler, AddEventReturnsSuccess) {
    std::vector<Event> events;
    std::mutex mtx;
    std::condition_variable cv;
    ConditionVariableDatabaseHandler h{events, mtx, cv};

    auto s = h.addEvent();
    EXPECT_EQ(s, status::success);
}

TEST(ConditionVariableHandler, HandlingEventReturnsCount) {
    std::vector<Event> events;
    std::mutex mtx;
    std::condition_variable cv;
    ConditionVariableDatabaseHandler h{events, mtx, cv};

    h.addEvent();
    auto count = h.handlingEvent();
    EXPECT_GE(count, 0u);
}
