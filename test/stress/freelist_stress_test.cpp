#include <gtest/gtest.h>
#include <set>
#include <thread>
#include <atomic>
#include <vector>
#include "stress/freelist_stress.hpp"

using namespace harness;
using namespace stress;
using logger::core::detail::FreeList;
using logger::core::detail::FreeNode;
using logger::core::detail::LogRecord;

struct FreeListTestConfig {
    StressConfig stress;
    std::size_t  pool_size;
};

class FreeListStressTest
    : public ::testing::TestWithParam<FreeListTestConfig>
{};

// Multiple threads pop concurrently — verify no duplicates, no lost nodes
TEST_P(FreeListStressTest, ConcurrentPopNoDuplicates) {
    auto [cfg, pool_size] = GetParam();
    FreeListStress stress{cfg, pool_size};

    auto result = stress.run();

    // Every iteration either got a record (success) or pool was empty (failure)
    EXPECT_EQ(result.total_iterations(),
              cfg.thread_count * cfg.iterations_per_thread);

    auto popped = stress.all_popped();

    // No duplicates: each node popped by exactly one thread
    std::set<LogRecord*> unique(popped.begin(), popped.end());
    EXPECT_EQ(unique.size(), popped.size())
        << "Duplicate nodes detected — CAS bug in try_pop";

    // popped + remaining in freelist = pool_size
    std::set<LogRecord*> remaining;
    FreeNode* node = nullptr;
    while ((node = stress.freelist().try_pop()) != nullptr) {
        remaining.insert(static_cast<LogRecord*>(node));
    }

    EXPECT_EQ(unique.size() + remaining.size(), pool_size)
        << "Lost nodes: popped=" << unique.size()
        << " remaining=" << remaining.size()
        << " pool=" << pool_size;
}

// With more threads than pool slots, some pops must fail
TEST_P(FreeListStressTest, ContentionCausesFailures) {
    auto [cfg, pool_size] = GetParam();

    // Only meaningful when total requests > pool_size
    std::size_t total = cfg.thread_count * cfg.iterations_per_thread;
    if (total <= pool_size) {
        GTEST_SKIP() << "Total requests fit in pool — no contention";
    }

    FreeListStress stress{cfg, pool_size};
    auto result = stress.run();

    // Can't pop more than pool_size total
    EXPECT_LE(result.total_success(), pool_size);
    EXPECT_GT(result.total_failure(), 0u)
        << "Expected some failures but got none";
}

// Production pattern: multiple poppers + single pusher (recycler)
// Simulates LogEngine: producers acquire records, worker recycles them
TEST_P(FreeListStressTest, MultiPopSinglePushRecycle) {
    auto [cfg, pool_size] = GetParam();

    FreeList fl;
    std::vector<LogRecord> pool(pool_size);
    for (auto& rec : pool) {
        fl.push(&rec);
    }

    std::size_t total_pops = cfg.thread_count * cfg.iterations_per_thread;

    // Shared channel: poppers put nodes here, pusher recycles them
    std::vector<std::atomic<LogRecord*>> channel(total_pops);
    for (auto& slot : channel) {
        slot.store(nullptr, std::memory_order_relaxed);
    }

    std::atomic<std::size_t> write_idx{0};
    std::atomic<bool> poppers_done{false};
    std::atomic<std::size_t> pop_success{0};

    // Barrier
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> go{false};

    // Popper threads: pop and hand off to channel
    auto popper = [&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) {}

        for (std::size_t i = 0; i < cfg.iterations_per_thread; ++i) {
            LogRecord* rec = static_cast<LogRecord*>(fl.try_pop());
            if (rec) {
                std::size_t idx = write_idx.fetch_add(1, std::memory_order_relaxed);
                if (idx < channel.size()) {
                    channel[idx].store(rec, std::memory_order_release);
                }
                pop_success.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    // Single pusher: recycles nodes from channel back to freelist
    auto pusher = [&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) {}

        std::size_t read_idx = 0;
        while (!poppers_done.load(std::memory_order_acquire)
               || read_idx < write_idx.load(std::memory_order_acquire))
        {
            if (read_idx < write_idx.load(std::memory_order_acquire)) {
                LogRecord* rec = channel[read_idx].load(std::memory_order_acquire);
                if (rec) {
                    fl.push(rec);
                    ++read_idx;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(cfg.thread_count + 1);
    for (std::size_t i = 0; i < cfg.thread_count; ++i) {
        threads.emplace_back(popper);
    }
    threads.emplace_back(pusher);

    while (ready.load(std::memory_order_acquire) < cfg.thread_count + 1) {}
    go.store(true, std::memory_order_release);

    // Wait for poppers first
    for (std::size_t i = 0; i < cfg.thread_count; ++i) {
        threads[i].join();
    }
    poppers_done.store(true, std::memory_order_release);

    // Wait for pusher
    threads.back().join();

    // With recycling, total successful pops can exceed pool_size
    EXPECT_GE(pop_success.load(), pool_size)
        << "Expected recycling to enable more pops than pool_size";

    // All nodes back in freelist
    std::set<LogRecord*> recovered;
    FreeNode* fn = nullptr;
    while ((fn = fl.try_pop()) != nullptr) {
        recovered.insert(static_cast<LogRecord*>(fn));
    }

    EXPECT_EQ(recovered.size(), pool_size)
        << "Lost nodes during multi-pop/single-push recycling";
}

INSTANTIATE_TEST_SUITE_P(
    FreeListVariants,
    FreeListStressTest,
    ::testing::Values(
        FreeListTestConfig{{.thread_count = 2, .iterations_per_thread = 500},  64},
        FreeListTestConfig{{.thread_count = 4, .iterations_per_thread = 1000}, 128},
        FreeListTestConfig{{.thread_count = 8, .iterations_per_thread = 2000}, 256}
    ),
    [](const auto& info) {
        return "t" + std::to_string(info.param.stress.thread_count)
             + "_i" + std::to_string(info.param.stress.iterations_per_thread)
             + "_p" + std::to_string(info.param.pool_size);
    }
);
