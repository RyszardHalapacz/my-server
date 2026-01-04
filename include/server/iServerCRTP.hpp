#pragma once

#include <cstdint>
#include <utility>

#include "event.hpp"  // Unified application-level event type

namespace server {

// =====================
// Server semantic types
// =====================

// High-level lifecycle state of the server instance.
enum class ServerState : uint8_t {
    created,    // constructed but not started
    running,    // accepting and processing events
    stopping,   // shutdown requested, draining in progress
    stopped,    // fully stopped, no background activity
    failed      // unrecoverable error state
};

// Shutdown behavior requested by the caller.
enum class ShutdownMode : uint8_t {
    graceful,   // finish processing queued events
    force       // drop pending events immediately
};

// Result of attempting to submit an event to the server.
enum class SubmitStatus : uint8_t {
    accepted,           // event accepted for processing
    rejected_full,      // rejected due to backpressure (queue full)
    rejected_stopped,   // rejected because server is not running
    invalid,            // rejected due to invalid event
    error               // internal error
};

// Snapshot of server observability counters.
// Returned by value to provide a consistent, read-only view.
struct ServerMetrics {
    uint64_t accepted_total          = 0;
    uint64_t rejected_full_total     = 0;
    uint64_t rejected_stopped_total  = 0;
    uint64_t invalid_total           = 0;
    uint64_t error_total             = 0;

    uint64_t queue_depth_snapshot    = 0;  // number of queued events at snapshot time
    uint64_t inflight_snapshot       = 0;  // number of events currently being processed
};

// =====================
// CRTP base: server contract (no vptr, no runtime polymorphism)
// Derived must implement the *Impl() methods.
// =====================

template <typename Derived>
class ServerCRTP {
public:
    // Starts the server (allocates resources, spawns threads, etc.).
    bool start() noexcept { 
        return d().startImpl(); 
    }

    // Requests server shutdown using the specified mode.
    void shutdown(ShutdownMode mode) noexcept { 
        d().shutdownImpl(mode); 
    }

    // Blocks until the server is fully stopped.
    bool wait() noexcept { 
        return d().waitImpl(); 
    }

    // Returns the current lifecycle state of the server.
    [[nodiscard]] ServerState state() const noexcept { 
        return d().stateImpl(); 
    }

    // Hot-path entry point.
    // Event is passed by value to allow move semantics at the call site.
    [[nodiscard]] SubmitStatus trySubmit(::Event ev) noexcept {
        return d().trySubmitImpl(std::move(ev));
    }

    // Returns a snapshot of server metrics.
    [[nodiscard]] ServerMetrics metrics() const noexcept { 
        return d().metricsImpl(); 
    }

    // Returns the effective concurrency level of the server
    // (e.g. number of worker threads).
    [[nodiscard]] uint32_t concurrency() const noexcept { 
        return d().concurrencyImpl(); 
    }

protected:
    // Protected non-virtual destructor:
    // prevents deletion through base type and enforces static polymorphism.
    ~ServerCRTP() = default;

private:
    // CRTP helpers for downcasting without runtime overhead.
    Derived& d() noexcept { 
        return static_cast<Derived&>(*this); 
    }

    const Derived& d() const noexcept { 
        return static_cast<const Derived&>(*this); 
    }
};

} // namespace server
