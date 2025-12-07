#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <ostream>
#include <type_traits>
#include <utility>
#include <new>
#include <iostream>
#include "logRecord.hpp"
#include "lockfreeQueue.hpp"
#include "publisher.hpp"
#include "stream_adapter.hpp"

namespace logger::core::detail
{
    class LogEngine
    {
    public:
        static LogEngine &instance() noexcept
        {
            static LogEngine eng;
            return eng;
        }

        uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
        uint64_t enqueued() const noexcept { return enqueued_.load(std::memory_order_relaxed); }
        uint64_t written() const noexcept { return written_.load(std::memory_order_relaxed); }

        // --------- MAIN API: transfer of the envelope ----------

        template <typename Envelope>
        void enqueue(Envelope &&env)
        {
            ensure_running();

            LogRecord *rec = acquire_record();
            if (!rec)
            {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            using E = std::decay_t<Envelope>;
            using Stored = StoredEnvelope<E>;

            static_assert(sizeof(Stored) <= LogRecord::StorageSize,
                          "Envelope too big for LogRecord::storage");
            static_assert(alignof(Stored) <= LogRecord::StorageAlign,
                          "StoredEnvelope alignment too strict");

            void *mem = rec->storage_ptr();

            // placement-new + std::move – ZERO copies of the envelope
            new (mem) Stored{std::move(env)};

            rec->process_fn = &process_impl<Stored>;
            rec->destroy_fn = &destroy_impl<Stored>;
            rec->submit_fn = &submit_impl<Stored>;

            push_to_queue(rec);
            enqueued_.fetch_add(1, std::memory_order_relaxed);
        }

        void shutdown() noexcept
        {
            stop_worker();
        }

    template<typename Stored>
    static void submit_impl(void* storage)
    {
        // 1) Cast void* → Stored* → Envelope&
        auto* obj = static_cast<Stored*>(storage);
        auto& env = obj->env;  // ← Typed envelope!
        
        // 2) Define adapter (envelope → string_view)
         using Envelope  = std::decay_t<decltype(env)>;
    using AdapterFn = std::string_view (*)(const Envelope&);

        AdapterFn adapter = [](const auto& envelope) -> std::string_view {
            thread_local std::string buffer;
            buffer.clear();
            
            std::ostringstream oss;
            envelope.debug_print(oss);
            buffer = oss.str();
            
            return std::string_view{buffer};
        };
        
        // 3) Call Publisher with TYPED envelope
        // TODO: Make Policy/Sink configurable via LogEngine::config_
        using Pub = Publisher<TerminalPolicy, TextSink>;
        Pub::publish(env, adapter);
    }

    private:
        LogEngine() = default;
        ~LogEngine() { stop_worker(); }

        LogEngine(const LogEngine &) = delete;
        LogEngine &operator=(const LogEngine &) = delete;

        // What actually resides in the storage – the envelope itself
        template <typename Envelope>
        struct StoredEnvelope
        {
            Envelope env;
        };

        // type-erased destructor
        template <typename Stored>
        static void destroy_impl(void *storage) noexcept
        {
            auto *obj = static_cast<Stored *>(storage);
            obj->~Stored();
        }

// type-erased processing: invoke the method on the envelope
        template <typename Stored>
        static void process_impl(void *storage, std::ostream &os)
        {
            auto *obj = static_cast<Stored *>(storage);
            auto &env = obj->env;




        }

        void ensure_running()
        {
            bool expected = false;
            if (run_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                init_pool_and_queue();
                worker_ = std::thread(&LogEngine::worker_loop, this);
            }
        }
        void init_pool_and_queue()
        {
           
            pool_size_ = 1024;
            pool_storage_ = std::make_unique<LogRecord[]>(pool_size_);

         
            for (std::size_t i = 0; i < pool_size_; ++i)
            {
                freelist_.push(&pool_storage_[i]);
            }
        }



        LogRecord *acquire_record()
        {
            return freelist_.try_pop(); 
        }

        void push_to_queue(LogRecord *rec)
        {
             queue_.push(rec);
        }

        void worker_loop()
        {
            using namespace std::chrono_literals;

            while (run_.load(std::memory_order_acquire) || !queue_.empty())
            {
                LogRecord *rec = queue_.pop();
                if (!rec)
                {

                    std::this_thread::sleep_for(50us);
                    continue;
                }

                std::ostringstream oss;
                rec->process_fn(rec->storage_ptr(), oss);

                rec->submit_fn(rec->storage_ptr());

                rec->destroy_fn(rec->storage_ptr());
                written_.fetch_add(1, std::memory_order_relaxed);
                freelist_.push(rec);
            }

            // Optionally drain any remaining records after run_ becomes false.
            LogRecord *rec = nullptr;
            while ((rec = queue_.pop()) != nullptr)
            {
                std::ostringstream oss;
                rec->process_fn(rec->storage_ptr(), oss);
                std::cout << oss.str() << '\n';

                rec->destroy_fn(rec->storage_ptr());
                written_.fetch_add(1, std::memory_order_relaxed);
                freelist_.push(rec);
            }
        }

        void stop_worker() noexcept
        {
            bool expected = true;
            if (run_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
            {
                if (worker_.joinable())
                    worker_.join();
            }
        }

    private:
        std::unique_ptr<LogRecord[]> pool_storage_;
        std::size_t pool_size_{0};

        FreeList freelist_;
        MpscQueue queue_;
        std::atomic<bool> run_{false};
        std::thread worker_;

        std::atomic<uint64_t> dropped_{0};
        std::atomic<uint64_t> enqueued_{0};
        std::atomic<uint64_t> written_{0};
    };

} // namespace logger::core::detail
