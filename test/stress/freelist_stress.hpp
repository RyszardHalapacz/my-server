#pragma once

#include "harness/stress_builder.hpp"
#include "logger/core/lockfree_queue.hpp"

#include <mutex>
#include <vector>

namespace stress {

using logger::core::detail::FreeList;
using logger::core::detail::FreeNode;
using logger::core::detail::LogRecord;

// Production pattern: multiple threads pop concurrently (acquire_record),
// popped nodes are collected per-thread for post-run verification.
// Push is NOT done concurrently with pop (matches LogEngine usage:
// producers pop, single worker pushes back).
class FreeListStress
    : public harness::StressBuilder<FreeListStress>
{
public:
    explicit FreeListStress(harness::StressConfig cfg, std::size_t pool_size)
        : StressBuilder(cfg)
        , pool_(pool_size)
        , pool_size_(pool_size)
        , per_thread_popped_(cfg.thread_count)
    {
        for (auto& rec : pool_) {
            freelist_.push(&rec);
        }
    }

    // Each thread: pop from freelist (acquire pattern)
    bool do_impl(std::size_t tid, std::size_t /*iteration*/) noexcept {
        LogRecord* rec = static_cast<LogRecord*>(freelist_.try_pop());
        if (!rec) {
            return false; // pool exhausted
        }
        per_thread_popped_[tid].push_back(rec);
        return true;
    }

    FreeList& freelist() noexcept { return freelist_; }
    std::vector<LogRecord>& pool() noexcept { return pool_; }
    std::size_t pool_size() const noexcept { return pool_size_; }

    // All popped nodes across all threads
    std::vector<LogRecord*> all_popped() const {
        std::vector<LogRecord*> result;
        for (auto& v : per_thread_popped_) {
            result.insert(result.end(), v.begin(), v.end());
        }
        return result;
    }

private:
    FreeList freelist_;
    std::vector<LogRecord> pool_;
    std::size_t pool_size_;
    std::vector<std::vector<LogRecord*>> per_thread_popped_;
};

} // namespace stress
