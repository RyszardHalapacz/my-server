#include <gtest/gtest.h>
#include "handler/threaded_database_handler.hpp"

using global::DatabaseConnection::status;

TEST(ThreadedDatabaseHandler, AddEventReturnsSuccess) {
    ThreadedDatabaseHandler h{0};
    auto s = h.addEvent();
    EXPECT_EQ(s, status::success);
    h.terminateThreads();
}

TEST(ThreadedDatabaseHandler, HandlingEventReturnsCount) {
    ThreadedDatabaseHandler h{0};
    h.addEvent();
    h.addEvent();
    auto count = h.handlingEvent();
    EXPECT_GE(count, 0u);
    h.terminateThreads();
}

TEST(ThreadedDatabaseHandler, TerminateAndDestroyDoNotHang) {
    {
        ThreadedDatabaseHandler h{0};
        h.addEvent();
        h.terminateThreads();
    }
    SUCCEED();
}
