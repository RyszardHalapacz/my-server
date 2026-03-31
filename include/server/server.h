#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "event.hpp"
#include "server/metrics/metrics.hpp"
#include "server/server_types.hpp"
#include "server/server_config.hpp"
#include "server/server_hooks.hpp"
#include "server/command.hpp"
#include "server/pipeline.hpp"

// ============================================================================
//  SERVER RUNTIME
//
//  Flat class, zero inheritance. Builder constructs at runtime.
//
//  - no data races on lifecycle state
//  - no std::deque + mutex on hot path
//  - no spin/yield worker loop
//  - config owned safely by value
//  - lower-level components use CRTP, server itself does not
//  - queue is injected as a concrete type (e.g. your MPSC queue)
//
//  Expected QueueT API:
//      bool tryPush(Event&&) noexcept;
//      bool tryPop(Event&) noexcept;
// ============================================================================

namespace server {

// ============================================================================
// Server
// ============================================================================
//
// QueueT is a concrete queue type supplied from outside.
// Expected API:
//
//     bool tryPush(Event&&) noexcept;
//     bool tryPop(Event&) noexcept;
//
// This lets you plug in your own MPSC queue.
//
// Server is intentionally non-copyable and non-movable.
// Builder returns std::unique_ptr<Server<...>>.
//

template <typename QueueT,
          typename ParserT,
          typename ExecutorT,
          typename DistributorT,
          typename... AdapterTs>
class Server final {
public:
    Server(ServerConfig config,
           ServerHookTable hooks,
           QueueT queue,
           ParserT parser,
           ExecutorT executor,
           DistributorT distributor,
           AdapterTs... adapters) noexcept
        : config_(std::move(config))
        , hooks_(std::move(hooks))
        , queue_(std::move(queue))
        , parser_(std::move(parser))
        , executor_(std::move(executor))
        , distributor_(std::move(distributor))
        , adapters_(std::move(adapters)...)
    {
        buildListenersFromConfig();
    }

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;

    ~Server() noexcept {
        shutdown(ShutdownMode::force);
        wait();
    }

    bool start() noexcept {
        ServerState expected = ServerState::created;
        if (!state_.compare_exchange_strong(expected,
                                            ServerState::running,
                                            std::memory_order_acq_rel)) {
            expected = ServerState::stopped;
            if (!state_.compare_exchange_strong(expected,
                                                ServerState::running,
                                                std::memory_order_acq_rel)) {
                return false;
            }
        }

        stopRequested_.store(false, std::memory_order_release);
        queuedApprox_.store(0, std::memory_order_release);

        const uint32_t workerCount =
            config_.execution.worker_count == 0 ? 1u : config_.execution.worker_count;

        workers_.reserve(workerCount);
        for (uint32_t i = 0; i < workerCount; ++i) {
            workers_.emplace_back([this]() noexcept {
                workerLoop();
            });
        }

        return true;
    }

    void shutdown(ShutdownMode mode) noexcept {
        shutdownMode_.store(mode, std::memory_order_release);

        auto old = state_.load(std::memory_order_acquire);
        if (old == ServerState::stopped || old == ServerState::failed) {
            return;
        }

        state_.store(ServerState::stopping, std::memory_order_release);
        stopRequested_.store(true, std::memory_order_release);

        wakeCv_.notify_all();
    }

    bool wait() noexcept {
        for (auto& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }

        workers_.clear();

        auto current = state_.load(std::memory_order_acquire);
        if (current != ServerState::failed) {
            state_.store(ServerState::stopped, std::memory_order_release);
        }

        return true;
    }

    [[nodiscard]] ServerState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    [[nodiscard]] SubmitStatus trySubmit(::Event ev) noexcept {
        if (hooks_.on_event_received) {
            hooks_.on_event_received(ev);
        }

        if (state_.load(std::memory_order_acquire) != ServerState::running) {
            metrics_.onRejectedStopped();
            if (hooks_.on_submit_result) {
                hooks_.on_submit_result(SubmitStatus::rejected_stopped);
            }
            return SubmitStatus::rejected_stopped;
        }

        if (!validateEvent(ev)) {
            metrics_.onInvalid();
            if (hooks_.on_submit_result) {
                hooks_.on_submit_result(SubmitStatus::invalid);
            }
            return SubmitStatus::invalid;
        }

        if (config_.execution.enable_backpressure) {
            const auto currentDepth = queuedApprox_.load(std::memory_order_relaxed);
            if (currentDepth >= config_.execution.queue_capacity) {
                metrics_.onRejectedFull();
                if (hooks_.on_submit_result) {
                    hooks_.on_submit_result(SubmitStatus::rejected_full);
                }
                return SubmitStatus::rejected_full;
            }
        }

        if (!queue_.tryPush(std::move(ev))) {
            metrics_.onRejectedFull();
            if (hooks_.on_submit_result) {
                hooks_.on_submit_result(SubmitStatus::rejected_full);
            }
            return SubmitStatus::rejected_full;
        }

        queuedApprox_.fetch_add(1, std::memory_order_release);
        metrics_.onAccepted();

        if (hooks_.on_submit_result) {
            hooks_.on_submit_result(SubmitStatus::accepted);
        }

        wakeCv_.notify_one();
        return SubmitStatus::accepted;
    }

    [[nodiscard]] ServerMetrics metrics() const noexcept {
        return metrics_.snapshot(queuedApprox_.load(std::memory_order_relaxed));
    }

    [[nodiscard]] uint32_t concurrency() const noexcept {
        return static_cast<uint32_t>(workers_.size());
    }

private:
    [[nodiscard]] bool validateEvent(const ::Event&) const noexcept {
        return true;
    }

    void buildListenersFromConfig() noexcept {
        listeners_.clear();
        listeners_.reserve(config_.listeners.size());

        for (const auto& cfg : config_.listeners) {
            listeners_.push_back(ListenerRuntime{
                .port = cfg.port,
                .protocol = cfg.protocol,
                .enabled = cfg.enabled
            });
        }
    }

    void workerLoop() noexcept {
        while (true) {
            ::Event ev;
            if (tryPopOne(ev)) {
                metrics_.inflightInc();
                processEvent(ev);
                metrics_.inflightDec();
                continue;
            }

            if (shouldExitWorker()) {
                break;
            }

            std::unique_lock<std::mutex> lk(wakeMutex_);
            wakeCv_.wait(lk, [this]() noexcept {
                return queuedApprox_.load(std::memory_order_acquire) > 0 ||
                       stopRequested_.load(std::memory_order_acquire);
            });
        }
    }

    [[nodiscard]] bool tryPopOne(::Event& out) noexcept {
        if (!queue_.tryPop(out)) {
            return false;
        }

        queuedApprox_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    [[nodiscard]] bool shouldExitWorker() const noexcept {
        if (!stopRequested_.load(std::memory_order_acquire)) {
            return false;
        }

        const auto mode = shutdownMode_.load(std::memory_order_acquire);
        if (mode == ShutdownMode::force) {
            return true;
        }

        return queuedApprox_.load(std::memory_order_acquire) == 0;
    }

    void processEvent(const ::Event& ev) noexcept {
        std::string_view rawInput;

        if (!decodeEvent(ev, rawInput)) {
            metrics_.onError();
            if (hooks_.on_error) {
                hooks_.on_error("decode failed");
            }
            return;
        }

        if (hooks_.on_raw_input) {
            hooks_.on_raw_input(rawInput);
        }

        command::Command cmd;
        if (!parser_.parse(rawInput, cmd)) {
            metrics_.onError();
            if (hooks_.on_error) {
                hooks_.on_error("parse failed");
            }
            return;
        }

        if (hooks_.on_command_built) {
            hooks_.on_command_built("command built");
        }

        result::Result res;
        if (!executor_.execute(cmd, res)) {
            metrics_.onError();
            if (hooks_.on_error) {
                hooks_.on_error("execution failed");
            }
            return;
        }

        if (hooks_.on_result_ready) {
            hooks_.on_result_ready(res.message);
        }

        distributor_.distribute(res);

        if (hooks_.on_distributed) {
            hooks_.on_distributed("result distributed");
        }
    }

    [[nodiscard]] bool decodeEvent(const ::Event& ev,
                                   std::string_view& out) noexcept {
        return decodeEventImpl<0>(ev, out);
    }

    template <std::size_t I>
    [[nodiscard]] bool decodeEventImpl(const ::Event& ev,
                                       std::string_view& out) noexcept {
        if constexpr (I >= sizeof...(AdapterTs)) {
            return false;
        } else {
            auto& adapter = std::get<I>(adapters_);
            if (adapter.decodeView(ev, out)) {
                return true;
            }
            return decodeEventImpl<I + 1>(ev, out);
        }
    }

private:
    // owned control-plane data
    ServerConfig config_;
    ServerHookTable hooks_;

    // concrete runtime pieces
    QueueT queue_;
    ParserT parser_;
    ExecutorT executor_;
    DistributorT distributor_;
    std::tuple<AdapterTs...> adapters_;

    // metrics (composition, not inheritance)
    metrics::MetricsMixin metrics_{};

    // lifecycle
    std::atomic<ServerState> state_{ServerState::created};
    std::atomic<ShutdownMode> shutdownMode_{ShutdownMode::graceful};
    std::atomic<bool> stopRequested_{false};

    // observability / wakeup
    std::atomic<uint64_t> queuedApprox_{0};
    std::mutex wakeMutex_{};
    std::condition_variable wakeCv_{};

    // runtime
    std::vector<ListenerRuntime> listeners_{};
    std::vector<std::thread> workers_{};
};

// ============================================================================
// Example queue stub
// Replace with your real MPSC queue
// ============================================================================

template <typename T>
class SimpleMutexQueue {
public:
    bool tryPush(T&& value) noexcept {
        std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(value));
        return true;
    }

    bool tryPop(T& out) noexcept {
        std::scoped_lock lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        out = std::move(queue_.front());
        queue_.erase(queue_.begin());
        return true;
    }

private:
    std::mutex mutex_{};
    std::vector<T> queue_{};
};

// ============================================================================
// Builder
// ============================================================================
//
// Builder validates required pieces and returns unique_ptr<Server<...>>.
// That avoids problems with returning non-movable Server by value.
//

template <typename QueueT,
          typename ParserT,
          typename ExecutorT,
          typename DistributorT,
          typename... AdapterTs>
class ServerBuilder {
public:
    ServerBuilder& withConfig(ServerConfig cfg) noexcept {
        config_ = std::move(cfg);
        hasConfig_ = true;
        return *this;
    }

    ServerBuilder& withHooks(ServerHookTable hooks) noexcept {
        hooks_ = std::move(hooks);
        return *this;
    }

    ServerBuilder& withQueue(QueueT queue) noexcept {
        queue_ = std::move(queue);
        hasQueue_ = true;
        return *this;
    }

    ServerBuilder& withParser(ParserT parser) noexcept {
        parser_ = std::move(parser);
        hasParser_ = true;
        return *this;
    }

    ServerBuilder& withExecutor(ExecutorT executor) noexcept {
        executor_ = std::move(executor);
        hasExecutor_ = true;
        return *this;
    }

    ServerBuilder& withDistributor(DistributorT distributor) noexcept {
        distributor_ = std::move(distributor);
        hasDistributor_ = true;
        return *this;
    }

    ServerBuilder& withAdapters(AdapterTs... adapters) noexcept {
        adapters_ = std::tuple<AdapterTs...>(std::move(adapters)...);
        hasAdapters_ = true;
        return *this;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return hasConfig_ &&
               hasQueue_ &&
               hasParser_ &&
               hasExecutor_ &&
               hasDistributor_ &&
               hasAdapters_;
    }

    [[nodiscard]] auto build() noexcept
        -> std::unique_ptr<Server<QueueT, ParserT, ExecutorT, DistributorT, AdapterTs...>>
    {
        if (!isValid()) {
            return nullptr;
        }

        return buildImpl(std::index_sequence_for<AdapterTs...>{});
    }

private:
    template <std::size_t... I>
    [[nodiscard]] auto buildImpl(std::index_sequence<I...>) noexcept
        -> std::unique_ptr<Server<QueueT, ParserT, ExecutorT, DistributorT, AdapterTs...>>
    {
        using ServerT = Server<QueueT, ParserT, ExecutorT, DistributorT, AdapterTs...>;

        return std::make_unique<ServerT>(
            std::move(config_),
            hooks_,
            std::move(queue_),
            std::move(parser_),
            std::move(executor_),
            std::move(distributor_),
            std::move(std::get<I>(adapters_))...
        );
    }

private:
    ServerConfig config_{};
    ServerHookTable hooks_ = hooks::makeNoopHooks();

    QueueT queue_{};
    ParserT parser_{};
    ExecutorT executor_{};
    DistributorT distributor_{};
    std::tuple<AdapterTs...> adapters_{};

    bool hasConfig_ = false;
    bool hasQueue_ = false;
    bool hasParser_ = false;
    bool hasExecutor_ = false;
    bool hasDistributor_ = false;
    bool hasAdapters_ = false;
};

} // namespace server
