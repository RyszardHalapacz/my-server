#include <gtest/gtest.h>
#include <type_traits>
#include <cstdint>
#include "logger/core/log_record.hpp"

using logger::core::detail::LogRecord;

TEST(LogRecord, Alignment) {
    static_assert(alignof(LogRecord) == 64);
    SUCCEED();
}

TEST(LogRecord, StorageConstants) {
    static_assert(LogRecord::StorageSize == 256);
    static_assert(LogRecord::StorageAlign == 64);
    SUCCEED();
}

TEST(LogRecord, NotPolymorphic) {
    static_assert(!std::is_polymorphic_v<LogRecord>);
    SUCCEED();
}

TEST(LogRecord, FunctionPointersDefaultToNull) {
    LogRecord rec{};
    EXPECT_EQ(rec.process_fn, nullptr);
    EXPECT_EQ(rec.destroy_fn, nullptr);
    EXPECT_EQ(rec.submit_fn, nullptr);
}

TEST(LogRecord, NextPointersDefaultToNull) {
    LogRecord rec{};
    EXPECT_EQ(rec.next.load(), nullptr);
    EXPECT_EQ(rec.free_next, nullptr);
}

TEST(LogRecord, StoragePtrReturnsAlignedAddress) {
    LogRecord rec{};
    void* p = rec.storage_ptr();
    EXPECT_NE(p, nullptr);
    auto addr = reinterpret_cast<std::uintptr_t>(p);
    EXPECT_EQ(addr % LogRecord::StorageAlign, 0u);
}

TEST(LogRecord, PlacementNewIntoStorage) {
    struct Small {
        uint64_t a;
        uint64_t b;
    };
    static_assert(sizeof(Small) <= LogRecord::StorageSize);

    LogRecord rec{};
    void* mem = rec.storage_ptr();
    auto* obj = new (mem) Small{42, 99};
    EXPECT_EQ(obj->a, 42u);
    EXPECT_EQ(obj->b, 99u);
    obj->~Small();
}
