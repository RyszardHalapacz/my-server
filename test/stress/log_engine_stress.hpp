#pragma once

#include "harness/stress_builder.hpp"
#include "logger/core/lockfree_queue.hpp"
#include "logger/core/log_record.hpp"
#include "logger/core/stream_adapter.hpp"
#include "common/messages/payloads/payloads.hpp"
#include "logger/registry/payload_register.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace stress {

using logger::core::detail::FreeList;
using logger::core::detail::MpscQueue;
using logger::core::detail::MpscNode;
using logger::core::detail::LogRecord;

// Instancjowalny pipeline identyczny z LogEngine, ale bez singletona.
// Pozwala na izolowane stress-testy.
class StressableLogEngine {
public:
    explicit StressableLogEngine(std::size_t pool_size = 1024)
        : pool_size_(pool_size)
        , pool_(std::make_unique<LogRecord[]>(pool_size))
    {
        for (std::size_t i = 0; i < pool_size_; ++i) {
            freelist_.push(&pool_[i]);
        }
    }

    ~StressableLogEngine() {
        shutdown();
    }

    StressableLogEngine(const StressableLogEngine&) = delete;
    StressableLogEngine& operator=(const StressableLogEngine&) = delete;

    void start() {
        bool expected = false;
        if (run_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            worker_ = std::thread(&StressableLogEngine::worker_loop, this);
        }
    }

    template <typename Envelope>
    bool enqueue(Envelope&& env) {
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

        rec->destroy_fn = [](void* s) noexcept {
            static_cast<Stored*>(s)->~Stored();
        };
        rec->submit_fn  = [](void*) {};

        queue_.push(rec);
        enqueued_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void shutdown() noexcept {
        bool expected = true;
        if (run_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            if (worker_.joinable()) {
                worker_.join();
            }
        }
    }

    uint64_t dropped()  const noexcept { return dropped_.load(std::memory_order_relaxed); }
    uint64_t enqueued() const noexcept { return enqueued_.load(std::memory_order_relaxed); }
    uint64_t written()  const noexcept { return written_.load(std::memory_order_relaxed); }
    std::size_t pool_size() const noexcept { return pool_size_; }

private:
    void worker_loop() {
        using namespace std::chrono_literals;

        LogRecord* pending_recycle = nullptr;

        while (run_.load(std::memory_order_acquire) || !queue_.empty()) {
            MpscNode* node = queue_.pop();
            if (!node) {
                std::this_thread::sleep_for(50us);
                continue;
            }
            LogRecord* rec = static_cast<LogRecord*>(node);

            if (pending_recycle) {
                freelist_.push(pending_recycle);
            }

            rec->destroy_fn(rec->storage_ptr());
            written_.fetch_add(1, std::memory_order_relaxed);

            pending_recycle = rec;
        }

        MpscNode* drain_node = nullptr;
        while ((drain_node = queue_.pop()) != nullptr) {
            LogRecord* rec = static_cast<LogRecord*>(drain_node);
            if (pending_recycle) {
                freelist_.push(pending_recycle);
            }

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
    FreeList freelist_;
    MpscQueue queue_;
    std::atomic<bool> run_{false};
    std::thread worker_;

    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> enqueued_{0};
    std::atomic<uint64_t> written_{0};

};

// Minimal envelope for stress testing — fits in LogRecord storage
struct StressEnvelope {
    std::size_t thread_id;
    std::size_t sequence;
};
static_assert(sizeof(StressEnvelope) <= LogRecord::StorageSize);

// StressBuilder derived: enqueue envelopes into StressableLogEngine
class LogEngineStress
    : public harness::StressBuilder<LogEngineStress>
{
public:
    explicit LogEngineStress(harness::StressConfig cfg,
                             std::size_t pool_size = 1024)
        : StressBuilder(cfg)
        , engine_(pool_size)
    {
        engine_.start();
    }

    bool do_impl(std::size_t tid, std::size_t iteration) noexcept {
        StressEnvelope env{tid, iteration};
        return engine_.enqueue(std::move(env));
    }

    StressableLogEngine& engine() noexcept { return engine_; }

private:
    StressableLogEngine engine_;
};

} // namespace stress
