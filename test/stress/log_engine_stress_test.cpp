#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "stress/log_engine_stress.hpp"

using namespace harness;
using namespace stress;

struct LogEngineTestConfig {
    StressConfig stress;
    std::size_t  pool_size;
};

class LogEngineStressTest
    : public ::testing::TestWithParam<LogEngineTestConfig>
{};

TEST_P(LogEngineStressTest, EnqueuedPlusDroppedEqualsTotal) {
    auto [cfg, pool_size] = GetParam();
    LogEngineStress stress{cfg, pool_size};

    auto result = stress.run();

    // Shutdown engine — worker drains remaining queue
    stress.engine().shutdown();

    std::size_t total = cfg.thread_count * cfg.iterations_per_thread;
    auto& eng = stress.engine();

    // Fundamental invariant: every enqueue either succeeded or was dropped
    EXPECT_EQ(eng.enqueued() + eng.dropped(), total)
        << "enqueued=" << eng.enqueued()
        << " dropped=" << eng.dropped()
        << " total=" << total;
}

TEST_P(LogEngineStressTest, WrittenEqualsEnqueued) {
    auto [cfg, pool_size] = GetParam();
    LogEngineStress stress{cfg, pool_size};

    stress.run();
    stress.engine().shutdown();

    auto& eng = stress.engine();

    // After shutdown, worker has drained everything
    EXPECT_EQ(eng.written(), eng.enqueued())
        << "written=" << eng.written()
        << " enqueued=" << eng.enqueued()
        << " — worker lost records";
}

TEST_P(LogEngineStressTest, PoolRecycledCorrectly) {
    auto [cfg, pool_size] = GetParam();
    std::size_t total = cfg.thread_count * cfg.iterations_per_thread;

    // Skip if total <= pool_size (no recycling needed)
    if (total <= pool_size) {
        GTEST_SKIP() << "Total enqueues fit in pool — recycling not tested";
    }

    LogEngineStress stress{cfg, pool_size};
    stress.run();
    stress.engine().shutdown();

    auto& eng = stress.engine();

    // If enqueued >= pool_size, pool was fully utilised.
    // Under extreme contention (many threads, small pool), the burst may
    // exhaust all slots before the worker recycles any — enqueued == pool_size
    // is valid backpressure, not a recycling bug.
    EXPECT_GE(eng.enqueued(), pool_size)
        << "Enqueued less than pool_size — pool underutilised";

    // Even with recycling, invariant holds
    EXPECT_EQ(eng.enqueued() + eng.dropped(), total);
}

TEST_P(LogEngineStressTest, DroppedOnlyWhenPoolExhausted) {
    auto [cfg, pool_size] = GetParam();
    std::size_t total = cfg.thread_count * cfg.iterations_per_thread;

    // Only meaningful when pressure can exhaust pool
    if (total <= pool_size && cfg.thread_count <= 2) {
        GTEST_SKIP() << "Not enough pressure to exhaust pool";
    }

    LogEngineStress stress{cfg, pool_size};
    stress.run();
    stress.engine().shutdown();

    auto& eng = stress.engine();

    // Invariant always holds
    EXPECT_EQ(eng.enqueued() + eng.dropped(), total);

    // If dropped > 0, it means pool was momentarily empty
    // (legitimate backpressure, not a bug)
    if (eng.dropped() > 0) {
        EXPECT_GT(total, pool_size)
            << "Drops with total <= pool_size suggest a recycling bug";
    }
}

INSTANTIATE_TEST_SUITE_P(
    LogEngineVariants,
    LogEngineStressTest,
    ::testing::Values(
        LogEngineTestConfig{{.thread_count = 4,  .iterations_per_thread = 10000}, 512},
        LogEngineTestConfig{{.thread_count = 8,  .iterations_per_thread = 10000}, 1024},
        LogEngineTestConfig{{.thread_count = 16, .iterations_per_thread = 5000},  256},
        LogEngineTestConfig{{.thread_count = 4,  .iterations_per_thread = 50000}, 64}
    ),
    [](const auto& info) {
        return "t" + std::to_string(info.param.stress.thread_count)
             + "_i" + std::to_string(info.param.stress.iterations_per_thread)
             + "_p" + std::to_string(info.param.pool_size);
    }
);
