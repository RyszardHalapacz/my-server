#pragma once

#include <atomic>
#include <cstdint>

namespace server {

struct ServerMetrics {
    uint64_t accepted_total          = 0;
    uint64_t rejected_full_total     = 0;
    uint64_t rejected_stopped_total  = 0;
    uint64_t invalid_total           = 0;
    uint64_t error_total             = 0;

    uint64_t queue_depth_snapshot    = 0;
    uint64_t inflight_snapshot       = 0;
};

namespace metrics {

struct MetricsMixin {
    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> rejected_full_{0};
    std::atomic<uint64_t> rejected_stopped_{0};
    std::atomic<uint64_t> invalid_{0};
    std::atomic<uint64_t> error_{0};
    std::atomic<uint64_t> inflight_{0};

    void onAccepted() noexcept { accepted_.fetch_add(1, std::memory_order_relaxed); }
    void onRejectedFull() noexcept { rejected_full_.fetch_add(1, std::memory_order_relaxed); }
    void onRejectedStopped() noexcept { rejected_stopped_.fetch_add(1, std::memory_order_relaxed); }
    void onInvalid() noexcept { invalid_.fetch_add(1, std::memory_order_relaxed); }
    void onError() noexcept { error_.fetch_add(1, std::memory_order_relaxed); }

    void inflightInc() noexcept { inflight_.fetch_add(1, std::memory_order_relaxed); }
    void inflightDec() noexcept { inflight_.fetch_sub(1, std::memory_order_relaxed); }

    [[nodiscard]] ServerMetrics snapshot(uint64_t queueDepth) const noexcept {
        ServerMetrics m;
        m.accepted_total          = accepted_.load(std::memory_order_relaxed);
        m.rejected_full_total     = rejected_full_.load(std::memory_order_relaxed);
        m.rejected_stopped_total  = rejected_stopped_.load(std::memory_order_relaxed);
        m.invalid_total           = invalid_.load(std::memory_order_relaxed);
        m.error_total             = error_.load(std::memory_order_relaxed);
        m.queue_depth_snapshot    = queueDepth;
        m.inflight_snapshot       = inflight_.load(std::memory_order_relaxed);
        return m;
    }
};

} // namespace metrics
} // namespace server
