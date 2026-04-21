#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <thread>

#include "logger/core/log_record.hpp"
#include "logger/core/lockfree_queue.hpp"

using logger::core::detail::FreeList;
using logger::core::detail::FreeNode;
using logger::core::detail::MpscQueue;
using logger::core::detail::MpscNode;
using logger::core::detail::LogRecord;

// Global submit counter — required because SubmitFn is a raw function pointer
// (no captures). Reset to 0 before each test.
inline std::atomic<uint64_t> g_submit_count{0};

class VerifiableEngine
{
public:
    explicit VerifiableEngine(std::size_t pool_size = 256)
        : pool_size_(pool_size)
        , pool_(std::make_unique<LogRecord[]>(pool_size))
    {
        for (std::size_t i = 0; i < pool_size_; ++i)
            freelist_.push(&pool_[i]);
    }

    ~VerifiableEngine() { shutdown(); }

    VerifiableEngine(const VerifiableEngine&) = delete;
    VerifiableEngine& operator=(const VerifiableEngine&) = delete;

    void start()
    {
        bool expected = false;
        if (run_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            worker_ = std::thread(&VerifiableEngine::worker_loop, this);
    }

    template <typename Envelope>
    bool enqueue(Envelope&& env)
    {
        LogRecord* rec = static_cast<LogRecord*>(freelist_.try_pop());
        if (!rec) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        using E = std::decay_t<Envelope>;
        struct Stored { E env; };
        static_assert(sizeof(Stored) <= LogRecord::StorageSize);
        static_assert(alignof(Stored) <= LogRecord::StorageAlign);

        new (rec->storage_ptr()) Stored{std::forward<Envelope>(env)};

        rec->submit_fn  = [](void*) {
            g_submit_count.fetch_add(1, std::memory_order_relaxed);
        };
        rec->destroy_fn = [](void* s) noexcept {
            static_cast<Stored*>(s)->~Stored();
        };

        queue_.push(rec);
        enqueued_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void shutdown() noexcept
    {
        bool expected = true;
        if (run_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
            if (worker_.joinable())
                worker_.join();
    }

    uint64_t enqueued() const noexcept { return enqueued_.load(std::memory_order_relaxed); }
    uint64_t written()  const noexcept { return written_.load(std::memory_order_relaxed); }
    uint64_t dropped()  const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    void worker_loop()
    {
        using namespace std::chrono_literals;
        LogRecord* pending_recycle = nullptr;

        while (run_.load(std::memory_order_acquire) || !queue_.empty()) {
            MpscNode* node = queue_.pop();
            if (!node) { std::this_thread::sleep_for(50us); continue; }
            LogRecord* rec = static_cast<LogRecord*>(node);

            if (pending_recycle) freelist_.push(pending_recycle);

            rec->submit_fn(rec->storage_ptr());
            rec->destroy_fn(rec->storage_ptr());
            written_.fetch_add(1, std::memory_order_relaxed);
            pending_recycle = rec;
        }

        MpscNode* node = nullptr;
        while ((node = queue_.pop()) != nullptr) {
            LogRecord* rec = static_cast<LogRecord*>(node);
            if (pending_recycle) freelist_.push(pending_recycle);

            rec->submit_fn(rec->storage_ptr());
            rec->destroy_fn(rec->storage_ptr());
            written_.fetch_add(1, std::memory_order_relaxed);
            pending_recycle = rec;
        }

        queue_.reset();
        if (pending_recycle)
            freelist_.push(pending_recycle);
    }

    std::size_t pool_size_;
    std::unique_ptr<LogRecord[]> pool_;
    FreeList  freelist_;
    MpscQueue queue_;
    std::atomic<bool>     run_{false};
    std::thread           worker_;
    std::atomic<uint64_t> enqueued_{0};
    std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> dropped_{0};
};

struct PipelineEnvelope { uint64_t id; };
static_assert(sizeof(PipelineEnvelope) <= LogRecord::StorageSize);
