#include <gtest/gtest.h>
#include "verifiable_engine.hpp"

// submit_fn is called for every enqueued record
TEST(LogEnginePipeline, SubmitFnCalledForEveryRecord)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr uint64_t N = 100;

    VerifiableEngine engine;
    engine.start();

    for (uint64_t i = 0; i < N; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();

    EXPECT_EQ(engine.enqueued(), N);
    EXPECT_EQ(engine.written(),  N);
    EXPECT_EQ(engine.dropped(),  0u);
    EXPECT_EQ(g_submit_count.load(), N);
}

// Records enqueued just before shutdown are processed by drain loop
TEST(LogEnginePipeline, DrainLoopProcessesAllPendingRecords)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr uint64_t N = 50;

    VerifiableEngine engine;
    engine.start();

    for (uint64_t i = 0; i < N; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();  // immediate — records may still be in queue

    EXPECT_EQ(engine.written(), engine.enqueued());
    EXPECT_EQ(g_submit_count.load(), engine.enqueued());
}

// Pool exhaustion — excess records are dropped, not corrupted
TEST(LogEnginePipeline, DropsWhenPoolExhausted)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr std::size_t pool_size    = 16;
    constexpr uint64_t    enqueue_count = 64;

    VerifiableEngine engine{pool_size};
    // do NOT start worker — pool won't be recycled
    // so after pool_size enqueues, rest must be dropped

    for (uint64_t i = 0; i < enqueue_count; ++i)
        engine.enqueue(PipelineEnvelope{i});

    EXPECT_EQ(engine.enqueued(), pool_size);
    EXPECT_EQ(engine.dropped(),  enqueue_count - pool_size);
}

// Pool is recycled — after processing, new records can be enqueued without drops
TEST(LogEnginePipeline, PoolRecycledAfterProcessing)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr std::size_t pool_size = 32;

    VerifiableEngine engine{pool_size};
    engine.start();

    // First batch — fill pool
    for (std::size_t i = 0; i < pool_size; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();
    ASSERT_EQ(engine.dropped(), 0u);

    // Restart and enqueue again — pool must have been recycled
    engine.start();
    for (std::size_t i = 0; i < pool_size; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();

    EXPECT_EQ(engine.dropped(), 0u);
    EXPECT_EQ(g_submit_count.load(), pool_size * 2);
}
