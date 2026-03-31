#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <new>
#include "log_record.hpp"
#include "lockfree_queue.hpp"
#include "publisher.hpp"
#include "stream_adapter.hpp"

namespace logger::core::detail
{
    class LogEngine
    {
    public:
        static LogEngine& instance() noexcept;

        uint64_t dropped()  const noexcept { return dropped_.load(std::memory_order_relaxed); }
        uint64_t enqueued() const noexcept { return enqueued_.load(std::memory_order_relaxed); }
        uint64_t written()  const noexcept { return written_.load(std::memory_order_relaxed); }

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

            rec->destroy_fn = &destroy_impl<Stored>;
            rec->submit_fn  = &submit_impl<Stored>;

            push_to_queue(rec);
            enqueued_.fetch_add(1, std::memory_order_relaxed);
        }

        void shutdown() noexcept;

    template<typename Stored>
    static void submit_impl(void* storage)
    {
        auto* obj = static_cast<Stored*>(storage);
        auto& env = obj->env;

        using Envelope  = std::decay_t<decltype(env)>;
        using AdapterFn = std::string_view (*)(const Envelope&);

        AdapterFn adapter = [](const Envelope& envelope) -> std::string_view {
            thread_local FixedStringBuf<1024> buf;
            thread_local std::ostream os(&buf);
            buf.reset();
            os.clear();

            envelope.debug_print(os);
            return buf.view();
        };

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


        void ensure_running();
        void init_pool_and_queue();
        LogRecord* acquire_record();
        void push_to_queue(LogRecord* rec);
        void worker_loop();
        void stop_worker() noexcept;

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
