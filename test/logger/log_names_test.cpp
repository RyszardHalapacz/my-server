#include <gtest/gtest.h>
#include "common/log_ids.hpp"

#include <gtest/gtest.h>
#include <string_view>
#include "common/log_ids.hpp"
#include "logger/log_names.hpp"


constexpr size_t kDefMethodCount =
0
#define X(C,F) + 1
#include "common/log_ids.def"
;
#undef X

TEST(LogIds, Sizes) {
    static_assert(sizeof(LogClassId) == 1, "LogClassId must be 1 byte");
    static_assert(sizeof(MethodId)   == 2, "MethodId must be 2 bytes");
    SUCCEED();
}

TEST(LogIds, MethodCountMatchesDef) {
    EXPECT_EQ(static_cast<size_t>(MethodId::Count), kDefMethodCount);
}

TEST(LogIds, NamesAndMappingSpecific) {
    // Froms .def:
    // X(Server,  AddEvent)
    // X(Handler, Run)
    // X(Handler, terminateThreads)
    // X(Handler, addEvent)

    EXPECT_EQ(methodName(MethodId::Server_AddEvent), "AddEvent");
    EXPECT_EQ(methodClass(MethodId::Server_AddEvent), LogClassId::Server);
    EXPECT_EQ(className(methodClass(MethodId::Server_AddEvent)), "Server");

    EXPECT_EQ(methodName(MethodId::Handler_Run), "Run");
    EXPECT_EQ(methodClass(MethodId::Handler_Run), LogClassId::Handler);
    EXPECT_EQ(className(methodClass(MethodId::Handler_Run)), "Handler");

    EXPECT_EQ(methodName(MethodId::Handler_terminateThreads), "terminateThreads");
    EXPECT_EQ(methodName(MethodId::Handler_addEvent),         "addEvent");
}

TEST(LogIds, AllEntriesHaveNames) {
    for (size_t i = 0; i < static_cast<size_t>(MethodId::Count); ++i) {
        auto m = static_cast<MethodId>(i);
        auto c = methodClass(m);
        auto mn = methodName(m);
        auto cn = className(c);
        EXPECT_FALSE(mn.empty()) << "Empty method name at index " << i;
        EXPECT_FALSE(cn.empty()) << "Empty class name for method index " << i;
    }
}
