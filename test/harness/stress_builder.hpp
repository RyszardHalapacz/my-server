#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

namespace harness {

// Per-thread result collected after run
struct ThreadResult {
    std::size_t thread_id      = 0;
    std::size_t success_count  = 0;
    std::size_t failure_count  = 0;
    std::chrono::nanoseconds elapsed{0};
};

// Aggregate results from all threads
struct RunResult {
    std::vector<ThreadResult> per_thread;
    std::chrono::nanoseconds  total_elapsed{0};

    [[nodiscard]] std::size_t total_success() const noexcept {
        std::size_t sum = 0;
        for (auto& t : per_thread) sum += t.success_count;
        return sum;
    }

    [[nodiscard]] std::size_t total_failure() const noexcept {
        std::size_t sum = 0;
        for (auto& t : per_thread) sum += t.failure_count;
        return sum;
    }

    [[nodiscard]] std::size_t total_iterations() const noexcept {
        return total_success() + total_failure();
    }
};

// Compile-time configuration for a stress run
struct StressConfig {
    std::size_t thread_count         = 4;
    std::size_t iterations_per_thread = 1000;
};

// ---------------------------------------------------------
// StressBuilder — CRTP base
//
// Derived must implement:
//   bool do_impl(std::size_t thread_id, std::size_t iteration)
//     — returns true on success, false on failure
//
// Derived may optionally override:
//   void setup_impl(std::size_t thread_id)
//   void teardown_impl(std::size_t thread_id)
// ---------------------------------------------------------

template <typename Derived>
class StressBuilder {
public:
    explicit constexpr StressBuilder(StressConfig cfg) noexcept
        : cfg_(cfg) {}

    RunResult run() {
        static_assert(!std::is_polymorphic_v<Derived>,
                      "StressBuilder: Derived must not be polymorphic");

        RunResult result;
        result.per_thread.resize(cfg_.thread_count);

        std::atomic<std::size_t> ready{0};
        std::atomic<bool> go{false};

        auto worker = [&](std::size_t tid) {
            // Per-thread setup
            self().setup_impl(tid);

            // Barrier: wait until all threads are ready
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
                // spin
            }

            auto t0 = std::chrono::steady_clock::now();

            std::size_t ok = 0;
            std::size_t fail = 0;

            for (std::size_t i = 0; i < cfg_.iterations_per_thread; ++i) {
                if (self().do_impl(tid, i)) {
                    ++ok;
                } else {
                    ++fail;
                }
            }

            auto t1 = std::chrono::steady_clock::now();

            result.per_thread[tid] = ThreadResult{
                .thread_id     = tid,
                .success_count = ok,
                .failure_count = fail,
                .elapsed       = t1 - t0
            };

            // Per-thread teardown
            self().teardown_impl(tid);
        };

        auto total_t0 = std::chrono::steady_clock::now();

        // Spawn threads
        std::vector<std::thread> threads;
        threads.reserve(cfg_.thread_count);
        for (std::size_t i = 0; i < cfg_.thread_count; ++i) {
            threads.emplace_back(worker, i);
        }

        // Wait for all threads to reach barrier
        while (ready.load(std::memory_order_acquire) < cfg_.thread_count) {
            // spin
        }

        // Release all threads simultaneously
        go.store(true, std::memory_order_release);

        // Join
        for (auto& t : threads) {
            t.join();
        }

        auto total_t1 = std::chrono::steady_clock::now();
        result.total_elapsed = total_t1 - total_t0;

        return result;
    }

    [[nodiscard]] constexpr const StressConfig& config() const noexcept {
        return cfg_;
    }

protected:
    // Default no-op hooks — Derived may override
    void setup_impl(std::size_t /*thread_id*/) noexcept {}
    void teardown_impl(std::size_t /*thread_id*/) noexcept {}

private:
    Derived& self() noexcept {
        return static_cast<Derived&>(*this);
    }

    StressConfig cfg_;
};

} // namespace harness
