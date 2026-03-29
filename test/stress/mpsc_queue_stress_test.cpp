#include <gtest/gtest.h>
#include <set>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include "stress/mpsc_queue_stress.hpp"

using namespace harness;
using namespace stress;
using logger::core::detail::LogRecord;
using logger::core::detail::MpscNode;
using logger::core::detail::MpscQueue;

class MpscQueueStressTest
    : public ::testing::TestWithParam<StressConfig>
{};

TEST_P(MpscQueueStressTest, AllRecordsArrivedNoDuplicates) {
    auto cfg = GetParam();
    MpscQueueStress stress{cfg};

    auto result = stress.run();

    std::size_t total = cfg.thread_count * cfg.iterations_per_thread;

    EXPECT_EQ(result.total_success(), total);
    EXPECT_EQ(result.total_failure(), 0u);

    std::set<LogRecord*> popped;
    MpscNode* node = nullptr;
    while ((node = stress.queue().pop()) != nullptr) {
        popped.insert(static_cast<LogRecord*>(node));
    }

    EXPECT_EQ(popped.size(), total);
    EXPECT_TRUE(stress.queue().empty());
}

TEST_P(MpscQueueStressTest, AllRecordsAreFromPreallocatedPool) {
    auto cfg = GetParam();
    MpscQueueStress stress{cfg};

    auto result = stress.run();

    auto& records = stress.records();
    LogRecord* first = records.data();
    LogRecord* last  = records.data() + records.size();

    MpscNode* node2 = nullptr;
    std::size_t count = 0;
    while ((node2 = stress.queue().pop()) != nullptr) {
        LogRecord* rec = static_cast<LogRecord*>(node2);
        EXPECT_GE(rec, first) << "Pointer below pool range";
        EXPECT_LT(rec, last)  << "Pointer above pool range";
        ++count;
    }

    EXPECT_EQ(count, cfg.thread_count * cfg.iterations_per_thread);
}

TEST_P(MpscQueueStressTest, FIFOPerProducer) {
    auto cfg = GetParam();
    MpscQueueStress stress{cfg};

    auto result = stress.run();

    // Drain queue and group sequences by thread_id
    std::map<std::size_t, std::vector<std::size_t>> per_thread;
    MpscNode* node3 = nullptr;
    while ((node3 = stress.queue().pop()) != nullptr) {
        LogRecord* rec = static_cast<LogRecord*>(node3);
        auto tag = MpscQueueStress::read_tag(rec);
        per_thread[tag.thread_id].push_back(tag.sequence);
    }

    // Each thread should have produced exactly iterations_per_thread records
    EXPECT_EQ(per_thread.size(), cfg.thread_count);

    for (auto& [tid, sequences] : per_thread) {
        EXPECT_EQ(sequences.size(), cfg.iterations_per_thread)
            << "Thread " << tid << " lost records";

        // Sequences must be strictly increasing (FIFO within one producer)
        for (std::size_t i = 1; i < sequences.size(); ++i) {
            EXPECT_GT(sequences[i], sequences[i - 1])
                << "FIFO violation for thread " << tid
                << " at position " << i
                << ": got " << sequences[i]
                << " after " << sequences[i - 1];
        }
    }
}

TEST_P(MpscQueueStressTest, ConcurrentPushPop) {
    auto cfg = GetParam();
    std::size_t total = cfg.thread_count * cfg.iterations_per_thread;

    // Shared queue and pre-allocated records
    MpscQueue queue;
    std::vector<LogRecord> records(total);

    std::atomic<bool> producers_done{false};
    std::atomic<std::size_t> push_count{0};

    // Consumer: runs in parallel with producers
    std::vector<LogRecord*> consumed;
    consumed.reserve(total);

    std::thread consumer([&] {
        while (!producers_done.load(std::memory_order_acquire)
               || !queue.empty())
        {
            MpscNode* n = queue.pop();
            if (n) {
                consumed.push_back(static_cast<LogRecord*>(n));
            }
        }

        // Final drain — catch records published between last empty() check
        // and producers_done flag
        MpscNode* n = nullptr;
        while ((n = queue.pop()) != nullptr) {
            consumed.push_back(static_cast<LogRecord*>(n));
        }
    });

    // Producers: use barrier for maximum contention
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> go{false};

    auto producer = [&](std::size_t tid) {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) {}

        for (std::size_t i = 0; i < cfg.iterations_per_thread; ++i) {
            std::size_t idx = tid * cfg.iterations_per_thread + i;
            queue.push(&records[idx]);
            push_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> producers;
    producers.reserve(cfg.thread_count);
    for (std::size_t i = 0; i < cfg.thread_count; ++i) {
        producers.emplace_back(producer, i);
    }

    // Wait for all producers to be ready, then release
    while (ready.load(std::memory_order_acquire) < cfg.thread_count) {}
    go.store(true, std::memory_order_release);

    // Wait for all producers to finish
    for (auto& t : producers) {
        t.join();
    }
    producers_done.store(true, std::memory_order_release);

    consumer.join();

    // Verify: all records consumed
    std::set<LogRecord*> unique(consumed.begin(), consumed.end());
    EXPECT_EQ(unique.size(), total)
        << "Lost or duplicated records under concurrent push/pop";
    EXPECT_EQ(consumed.size(), total);
    EXPECT_TRUE(queue.empty());
}

INSTANTIATE_TEST_SUITE_P(
    MpscQueueVariants,
    MpscQueueStressTest,
    ::testing::Values(
        StressConfig{.thread_count = 1, .iterations_per_thread = 1000},
        StressConfig{.thread_count = 2, .iterations_per_thread = 500},
        StressConfig{.thread_count = 4, .iterations_per_thread = 1000},
        StressConfig{.thread_count = 8, .iterations_per_thread = 2000}
    ),
    [](const auto& info) {
        auto cfg = info.param;
        return "t" + std::to_string(cfg.thread_count)
             + "_i" + std::to_string(cfg.iterations_per_thread);
    }
);
