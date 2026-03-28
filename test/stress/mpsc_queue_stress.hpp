#pragma once

#include "harness/stress_builder.hpp"
#include "logger/core/lockfree_queue.hpp"

#include <cstddef>
#include <new>
#include <vector>

namespace stress {

using logger::core::detail::MpscQueue;
using logger::core::detail::LogRecord;

// Tag stored via placement-new in LogRecord::storage
struct RecordTag {
    std::size_t thread_id;
    std::size_t sequence;
};
static_assert(sizeof(RecordTag) <= LogRecord::StorageSize,
              "RecordTag must fit in LogRecord storage");

class MpscQueueStress
    : public harness::StressBuilder<MpscQueueStress>
{
public:
    explicit MpscQueueStress(harness::StressConfig cfg)
        : StressBuilder(cfg)
        , records_(cfg.thread_count * cfg.iterations_per_thread)
    {}

    // Called by each producer thread: tag + push
    bool do_impl(std::size_t tid, std::size_t iteration) noexcept {
        std::size_t idx = tid * config().iterations_per_thread + iteration;
        auto* rec = &records_[idx];

        // Tag the record so consumer can verify origin and order
        new (rec->storage_ptr()) RecordTag{tid, iteration};

        queue_.push(rec);
        return true;
    }

    // Read tag from a popped record
    static RecordTag read_tag(LogRecord* rec) noexcept {
        return *static_cast<RecordTag*>(rec->storage_ptr());
    }

    MpscQueue& queue() noexcept { return queue_; }
    std::vector<LogRecord>& records() noexcept { return records_; }

private:
    MpscQueue queue_;
    std::vector<LogRecord> records_;
};

} // namespace stress
