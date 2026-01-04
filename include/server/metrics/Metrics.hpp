#pragma once

#include <atomic>
#include <cstdint>

#include "server/iServerCRTP.hpp"  // daje server::ServerMetrics

namespace server::metrics {

struct MetricsMixin {
    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> rejectedFull_{0};
    std::atomic<uint64_t> rejectedStopped_{0};
    std::atomic<uint64_t> invalid_{0};
    std::atomic<uint64_t> error_{0};
    std::atomic<uint64_t> inflight_{0};

    void onAccepted() noexcept { accepted_.fetch_add(1, std::memory_order_relaxed); }
    void onRejectedFull() noexcept { rejectedFull_.fetch_add(1, std::memory_order_relaxed); }
    void onRejectedStopped() noexcept { rejectedStopped_.fetch_add(1, std::memory_order_relaxed); }
    void onInvalid() noexcept { invalid_.fetch_add(1, std::memory_order_relaxed); }
    void onError() noexcept { error_.fetch_add(1, std::memory_order_relaxed); }

    void inflightInc() noexcept { inflight_.fetch_add(1, std::memory_order_relaxed); }
    void inflightDec() noexcept { inflight_.fetch_sub(1, std::memory_order_relaxed); }

    // UWAGA: zwracamy server::ServerMetrics (jeden jedyny typ metryk w systemie)
    [[nodiscard]] server::ServerMetrics snapshot(uint64_t queueDepth) const noexcept {
        server::ServerMetrics m;
        m.accepted_total          = accepted_.load(std::memory_order_relaxed);
        m.rejected_full_total     = rejectedFull_.load(std::memory_order_relaxed);
        m.rejected_stopped_total  = rejectedStopped_.load(std::memory_order_relaxed);
        m.invalid_total           = invalid_.load(std::memory_order_relaxed);
        m.error_total             = error_.load(std::memory_order_relaxed);
        m.queue_depth_snapshot    = queueDepth;
        m.inflight_snapshot       = inflight_.load(std::memory_order_relaxed);
        return m;
    }
};

} // namespace server::metrics
