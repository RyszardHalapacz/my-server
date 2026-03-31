#include <chrono>
#include <iostream>
#include "logger/core/log_engine.hpp"

namespace logger::core::detail
{

LogEngine& LogEngine::instance() noexcept
{
    static LogEngine eng;
    return eng;
}

void LogEngine::shutdown() noexcept
{
    stop_worker();
}

void LogEngine::ensure_running()
{
    bool expected = false;
    if (run_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    {
        init_pool_and_queue();
        worker_ = std::thread(&LogEngine::worker_loop, this);
    }
}

void LogEngine::init_pool_and_queue()
{
    pool_size_    = 1024;
    pool_storage_ = std::make_unique<LogRecord[]>(pool_size_);

    for (std::size_t i = 0; i < pool_size_; ++i)
        freelist_.push(&pool_storage_[i]);
}

LogRecord* LogEngine::acquire_record()
{
    FreeNode* node = freelist_.try_pop();
    return node ? static_cast<LogRecord*>(node) : nullptr;
}

void LogEngine::push_to_queue(LogRecord* rec)
{
    queue_.push(rec);
}

void LogEngine::worker_loop()
{
    using namespace std::chrono_literals;

    LogRecord* pending_recycle = nullptr;

    while (run_.load(std::memory_order_acquire) || !queue_.empty())
    {
        MpscNode* node = queue_.pop();
        if (!node)
        {
            std::this_thread::sleep_for(50us);
            continue;
        }
        LogRecord* rec = static_cast<LogRecord*>(node);

        if (pending_recycle)
            freelist_.push(pending_recycle);

        rec->submit_fn(rec->storage_ptr());
        rec->destroy_fn(rec->storage_ptr());
        written_.fetch_add(1, std::memory_order_relaxed);

        pending_recycle = rec;
    }

    MpscNode* node = nullptr;
    while ((node = queue_.pop()) != nullptr)
    {
        LogRecord* rec = static_cast<LogRecord*>(node);
        if (pending_recycle)
            freelist_.push(pending_recycle);

        rec->submit_fn(rec->storage_ptr());
        rec->destroy_fn(rec->storage_ptr());
        written_.fetch_add(1, std::memory_order_relaxed);

        pending_recycle = rec;
    }

    queue_.reset();
    if (pending_recycle)
        freelist_.push(pending_recycle);
}

void LogEngine::stop_worker() noexcept
{
    bool expected = true;
    if (run_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
    {
        if (worker_.joinable())
            worker_.join();
    }
}

} // namespace logger::core::detail
