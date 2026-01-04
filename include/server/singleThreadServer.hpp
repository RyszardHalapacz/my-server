// include/server/SingleThreadServer.hpp
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "iServerCRTP.hpp"
#include "server/metrics/Metrics.hpp"
#include "event.hpp"

namespace server {

// A minimal server implementation using a single worker thread.
// Events are queued and processed sequentially by one background thread.
// This class demonstrates the concrete implementation of the ServerCRTP contract.
class SingleThreadServer final
    : public ServerCRTP<SingleThreadServer>
    , public server::metrics::MetricsMixin
{
public:
    // Constructs the server with a fixed queue capacity.
    // When the queue is full, new events are rejected with backpressure.
    explicit SingleThreadServer(std::size_t queueCapacity = 1024) noexcept
        : queueCapacity_(queueCapacity) {}

    // Destructor enforces a forceful shutdown to guarantee
    // that the worker thread is stopped before destruction.
    ~SingleThreadServer() noexcept {
        shutdownImpl(ShutdownMode::force);
        (void)waitImpl();
    }

    // =====================
    // CRTP implementation
    // =====================

    // Starts the server and spawns the worker thread.
    // Returns false if the server was already started.
    bool startImpl() noexcept {
        ServerState expected = ServerState::created;
        if (!state_.compare_exchange_strong(
                expected, ServerState::running, std::memory_order_acq_rel)) {
            // If already running, treat start as idempotent
            return (expected == ServerState::running);
        }

        stopRequested_.store(false, std::memory_order_release);
        worker_ = std::thread([this] { workerLoop_(); });
        return true;
    }

    // Requests server shutdown.
    // In graceful mode, queued events are drained.
    // In force mode, the queue is cleared immediately.
    void shutdownImpl(ShutdownMode mode) noexcept {
        auto s = state_.load(std::memory_order_acquire);
        if (s == ServerState::stopped || s == ServerState::created) {
            state_.store(ServerState::stopped, std::memory_order_release);
            return;
        }

        state_.store(ServerState::stopping, std::memory_order_release);
        stopRequested_.store(true, std::memory_order_release);

        if (mode == ShutdownMode::force) {
            std::scoped_lock lk(mtx_);
            queue_.clear();
        }

        // Wake up the worker thread so it can exit.
        cv_.notify_all();
    }

    // Blocks until the worker thread exits and the server is fully stopped.
    bool waitImpl() noexcept {
        if (worker_.joinable()) {
            worker_.join();
        }
        state_.store(ServerState::stopped, std::memory_order_release);
        return true;
    }

    // Returns the current lifecycle state of the server.
    [[nodiscard]] ServerState stateImpl() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // Attempts to enqueue an event for processing.
    // This is the hot-path entry point called by listeners or tests.
    [[nodiscard]] SubmitStatus trySubmitImpl(Event&& ev) noexcept {
        if (stateImpl() != ServerState::running) {
            onRejectedStopped();
            return SubmitStatus::rejected_stopped;
        }

        // Optional event validation can be performed here.
        // If validation is moved out of the core, this block can be removed.
        // Example:
        // if (ev.id == 0) { onInvalid(); return SubmitStatus::invalid; }

        {
            std::scoped_lock lk(mtx_);
            if (queue_.size() >= queueCapacity_) {
                onRejectedFull();
                return SubmitStatus::rejected_full;
            }

            queue_.push_back(std::move(ev));
        }

        onAccepted();
        cv_.notify_one();
        return SubmitStatus::accepted;
    }

    // Returns a snapshot of server metrics, including current queue depth.
    [[nodiscard]] ServerMetrics metricsImpl() const noexcept {
        uint64_t q = 0;
        {
            std::scoped_lock lk(mtx_);
            q = static_cast<uint64_t>(queue_.size());
        }
        return snapshot(q); // provided by MetricsMixin
    }

    // Single-threaded server always reports concurrency = 1.
    [[nodiscard]] uint32_t concurrencyImpl() const noexcept {
        return 1;
    }

private:
    // Worker thread main loop.
    // Waits for events or shutdown signal, then processes events sequentially.
    void workerLoop_() noexcept {
        for (;;) {
            Event ev{};
            {
                std::unique_lock lk(mtx_);
                cv_.wait(lk, [&] {
                    return stopRequested_.load(std::memory_order_acquire)
                           || !queue_.empty();
                });

                // Graceful shutdown condition:
                // stop requested and no more events to process.
                if (stopRequested_.load(std::memory_order_acquire)
                    && queue_.empty()) {
                    break;
                }

                ev = std::move(queue_.front());
                queue_.pop_front();
                inflightInc();
            }

            // Process a single event.
            handleEvent_(ev);

            inflightDec();
        }
    }

    // Placeholder for actual event handling logic.
    // In a real server, this would dispatch to handlers or business logic.
    void handleEvent_(const Event& /*ev*/) noexcept {
        // intentionally empty
    }

private:
    // Maximum number of events allowed in the queue.
    const std::size_t queueCapacity_;

    // Synchronization primitives protecting the queue.
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Event> queue_;

    // Background worker thread.
    std::thread worker_;

    // Shutdown coordination and lifecycle state.
    std::atomic<bool> stopRequested_{false};
    std::atomic<ServerState> state_{ServerState::created};
};

} // namespace server
