#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "comon.h"
#include "ThreadedDatabaseHandler.hpp"
#include "ConditionVariableDatabaseHandler.hpp"

// ========= Base (generic) =========
template <typename HandlerType>
class ServerBase {
public:
    ServerBase() noexcept = default;
    virtual ~ServerBase() noexcept
    {
        for (auto& h : vecHandler) {
            if (h) h->terminateThreads();  //check if h is not nulltpr
        }
    }

    ServerBase(const ServerBase&) = delete;
    ServerBase& operator=(const ServerBase&) = delete;
    ServerBase(ServerBase&&) = delete;
    ServerBase& operator=(ServerBase&&) = delete;

    [[nodiscard]] uint32_t getMaxThread() const noexcept { return maxThreads_; }

    virtual int addEvent(Event event) = 0;

protected:
    static uint32_t hwThreadsOrOne() noexcept
    {
        auto n = std::thread::hardware_concurrency();
        return n ? n : 1;
    }

    uint32_t maxThreads_ = hwThreadsOrOne();
    std::atomic<uint32_t> requestCounter_{1};         
    std::vector<std::unique_ptr<HandlerType>> vecHandler;
    mutable std::mutex mutex_;                     
};

// ========= Base<void> specialization =========
template <>
class ServerBase<void> {
public:
    ServerBase() noexcept = default;
    virtual ~ServerBase() noexcept = default;

    ServerBase(const ServerBase&) = delete;
    ServerBase& operator=(const ServerBase&) = delete;
    ServerBase(ServerBase&&) = delete;
    ServerBase& operator=(ServerBase&&) = delete;

    
    virtual int addEvent(Event event) = 0;

protected:
    std::atomic<uint32_t> requestCounter_{0};
};

// ========= ServerConditionVar =========
class ServerConditionVar : public ServerBase<ConditionVariableDatabaseHandler> {
public:
    ServerConditionVar() {
        for (uint32_t i = 0; i < this->maxThreads_; ++i) {
            this->vecHandler.emplace_back(
                std::make_unique<ConditionVariableDatabaseHandler>(queue_, this->mutex_, cv_));
        }
    }

    int addEvent(Event event) override {
        {
            std::lock_guard<std::mutex> lock(this->mutex_);
            queue_.emplace_back(/* TODO */);
        }
        cv_.notify_one();
        return  1; 
    }

private:
    std::vector<Event>       queue_;
    std::condition_variable  cv_;
};

// ========= ServerThreaded =========
class ServerThreaded : public ServerBase<ThreadedDatabaseHandler> {
public:
    ServerThreaded() {
        for (uint32_t i = 0; i < this->maxThreads_; ++i) 
        {
            this->vecHandler.emplace_back(std::make_unique<ThreadedDatabaseHandler>(i));
        }
    }

    int addEvent(Event event) override {
        const uint32_t ticket = this->requestCounter_.fetch_add(1, std::memory_order_relaxed);
        const uint32_t idx    = ticket % this->maxThreads_;
        this->vecHandler[idx]->addEvent(); 
        return  1; // TODO: status
    }
};

// ========= ServerAsync =========
class ServerAsync : public ServerBase<void> {
public:
    ServerAsync() = default;

  int addEvent(Event event) override {
    const uint32_t ticket = requestCounter_.fetch_add(1, std::memory_order_relaxed);

    futures_.emplace_back(std::async(std::launch::async,
        [ev = std::move(event), ticket]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::cout << "Handled event " << ticket
                      << " in thread: " << std::this_thread::get_id() << "\n";
            return  ev;
        }));

    return /*np. status*/ 0;
}

    ~ServerAsync() noexcept override = default; 

private:
    std::vector<std::future<Event>> futures_;
};

// ========= ServerSingleThread =========
class ServerSingleThread : public ServerBase<void> {
public:
    int addEvent(Event event) override {
        std::cout << "Handled event " << ++requestCounter_
                  << " in main thread: " << std::this_thread::get_id() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return  1; // TODO: status
    }
};
