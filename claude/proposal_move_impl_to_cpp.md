# Proposal: Move non-template implementations from headers to .cpp

**Date:** 2026-03-31

---

## Original Prompt

> sprawdz pliki naglowkoe i zobacz co mozesz przenisc z plikow naglokwoych do cpp w foderze /src/

---

## Context

Kilka plików nagłówkowych zawiera implementacje metod, które nie wymagają pozostania w headerze (nie są template, nie są constexpr, nie są na tyle krótkie, żeby inlining był krytyczny). Przeniesienie ich do `.cpp` skraca czas kompilacji, zmniejsza zależności między TU i ukrywa implementację.

Trzy pliki z wyraźnymi kandydatami:

1. `include/logger/core/log_engine.hpp` — klasa `LogEngine` **nie jest template**. Większość metod (worker loop, pool init, itp.) to implementacje w headerze.
2. `include/publisher/sink_publisher.hpp` — `TextSink::format_impl` ma 70 linii logiki parsowania, `format_timestamp_us` 14 linii. `JsonSink::format_impl` — 1 linia.
3. `include/logger/registry/payload_register.hpp` — `GenericPayload::debug_impl` i `RequestPayload::debug_impl` — trywialne 2-linery, ale dla spójności.

---

## Proposed Changes

### Change 1: `include/logger/core/log_engine.hpp` + `src/logger/core/log_engine.cpp` (nowy plik) — przenieś nietemplate metody LogEngine

**What:** Przenieś deklaracje → implementacje dla 8 metod `LogEngine`:
- `static LogEngine& instance() noexcept`
- `void shutdown() noexcept`
- `void ensure_running()`
- `void init_pool_and_queue()`
- `LogRecord* acquire_record()`
- `void push_to_queue(LogRecord*)`
- `void worker_loop()`
- `void stop_worker() noexcept`

**Why:** `LogEngine` jest zwykłą klasą (nie template). Implementacje w headerze są kompilowane w każdej TU, która includuje `log_engine.hpp`. Worker loop (47 linii) + init (12 linii) to konkretny koszt.

**Impact:** Szybsza kompilacja, ukryta implementacja. Zero wpływu na runtime — metody nie są na hot path (enqueue deleguje do private helpers, ale `ensure_running()` sprawdza atomic; jednak samo ciało tych metod i tak nie będzie inline — kompilator i tak je wyoutline'uje przy takim rozmiarze).

**Proposed code — `include/logger/core/log_engine.hpp` (po zmianie):**
```cpp
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

        template <typename Envelope>
        void enqueue(Envelope&& env)
        {
            ensure_running();

            LogRecord* rec = acquire_record();
            if (!rec)
            {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            using E      = std::decay_t<Envelope>;
            using Stored = StoredEnvelope<E>;

            static_assert(sizeof(Stored) <= LogRecord::StorageSize,
                          "Envelope too big for LogRecord::storage");
            static_assert(alignof(Stored) <= LogRecord::StorageAlign,
                          "StoredEnvelope alignment too strict");

            void* mem = rec->storage_ptr();
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

            using Pub = Publisher<TerminalPolicy, TextSink>;
            Pub::publish(env, adapter);
        }

    private:
        LogEngine() = default;
        ~LogEngine() { stop_worker(); }

        LogEngine(const LogEngine&) = delete;
        LogEngine& operator=(const LogEngine&) = delete;

        template <typename Envelope>
        struct StoredEnvelope
        {
            Envelope env;
        };

        template <typename Stored>
        static void destroy_impl(void* storage) noexcept
        {
            auto* obj = static_cast<Stored*>(storage);
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

        FreeList  freelist_;
        MpscQueue queue_;
        std::atomic<bool> run_{false};
        std::thread worker_;

        std::atomic<uint64_t> dropped_{0};
        std::atomic<uint64_t> enqueued_{0};
        std::atomic<uint64_t> written_{0};
    };

} // namespace logger::core::detail
```

**Proposed code — `src/logger/core/log_engine.cpp` (nowy plik):**
```cpp
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
```

#### Review & Status
- [ LogEngine — zgoda

To jest najważniejsza zmiana.

LogEngine nie jest klasą szablonową.
Zawiera metody o realnej logice (worker_loop, init_pool_and_queue, itd.).
Obecnie ich implementacje trafiają do każdej TU includującej header.

Przeniesienie do .cpp:

ograniczy czas kompilacji,
zmniejszy rozmiar kodu generowanego w TU,
poprawi enkapsulację.

Zostawienie w headerze:

enqueue
submit_impl
destroy_impl
StoredEnvelope

jest poprawne, bo są template.

Wniosek:
To jest zmiana o realnej wartości i powinna zostać wdrożona.] Awaiting review

---

### Change 2: `include/publisher/sink_publisher.hpp` + `src/publisher/sink_publisher.cpp` (nowy plik) — przenieś implementacje TextSink i JsonSink

**What:** Przenieś `TextSink::format_impl` (70 linii), `TextSink::format_timestamp_us` (14 linii) i `JsonSink::format_impl` (1 linia) do `.cpp`. W headerze zostają tylko deklaracje.

**Why:** `TextSink::format_impl` zawiera 70 linii logiki parsowania tekstu, `format_timestamp_us` używa `<chrono>`, `<ctime>`, `<sstream>`, `<iomanip>`. Żadna z tych metod nie jest template. Przeniesienie ukrywa implementację i usuwa ciężkie includey z headera.

**Impact:** Header `sink_publisher.hpp` może wyrzucić `<sstream>`, `<iomanip>`, `<chrono>`, `<ctime>` — zmniejsza się zakres includeów dla każdej TU includującej ten header.

**Proposed code — `include/publisher/sink_publisher.hpp` (po zmianie):**
```cpp
///////////////////////////////////////
// SinkBase + concrete sinks
///////////////////////////////////////
#pragma once
#include <string>
#include <string_view>

#include "logger/log_names.hpp"
#include "common/log_ids.hpp"

template<typename Derived>
struct SinkBase {
    using view_type = std::string_view;

    static std::string format(view_type line) {
        return Derived::format_impl(line);
    }
};

// JSON sink
struct JsonSink : SinkBase<JsonSink> {
    using view_type = std::string_view;
    static std::string format_impl(view_type line);
};

// TEXT sink
struct TextSink : SinkBase<TextSink> {
    using view_type = std::string_view;
    static std::string format_impl(view_type line);
private:
    static std::string format_timestamp_us(std::uint64_t us_since_epoch);
};
```

**Proposed code — `src/publisher/sink_publisher.cpp` (nowy plik):**
```cpp
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

#include "publisher/sink_publisher.hpp"

// ---- JsonSink ----

std::string JsonSink::format_impl(view_type line)
{
    return "JsonTest" + std::string(line);
}

// ---- TextSink ----

std::string TextSink::format_impl(view_type line)
{
    std::string result;
    result.reserve(line.size() + 100);

    size_t pos = 0;

    while (pos < line.size()) {
        auto ts_pos = line.find("timestamp=", pos);
        if (ts_pos != std::string_view::npos) {
            result.append(line.substr(pos, ts_pos - pos));
            result.append("timestamp=");

            auto num_start = ts_pos + 10;
            auto num_end   = line.find(' ', num_start);
            if (num_end == std::string_view::npos) num_end = line.size();

            auto ts_str = line.substr(num_start, num_end - num_start);
            std::uint64_t us = std::stoull(std::string(ts_str));
            result.append(format_timestamp_us(us));

            pos = num_end;
            continue;
        }

        auto class_pos = line.find("class_id=", pos);
        if (class_pos != std::string_view::npos && (ts_pos == std::string_view::npos || class_pos < ts_pos)) {
            result.append(line.substr(pos, class_pos - pos));
            result.append("class_id=");

            auto num_start = class_pos + 9;
            auto num_end   = line.find(' ', num_start);
            if (num_end == std::string_view::npos) num_end = line.size();

            auto id_str = line.substr(num_start, num_end - num_start);
            auto id = static_cast<LogClassId>(std::stoi(std::string(id_str)));
            result.append(className(id));

            pos = num_end;
            continue;
        }

        auto method_pos = line.find("method_id=", pos);
        if (method_pos != std::string_view::npos &&
            (ts_pos == std::string_view::npos || method_pos < ts_pos) &&
            (class_pos == std::string_view::npos || method_pos < class_pos)) {
            result.append(line.substr(pos, method_pos - pos));
            result.append("method_id=");

            auto num_start = method_pos + 10;
            auto num_end   = line.find(' ', num_start);
            if (num_end == std::string_view::npos) num_end = line.size();

            auto id_str = line.substr(num_start, num_end - num_start);
            auto id = static_cast<MethodId>(std::stoi(std::string(id_str)));
            result.append(methodName(id));

            pos = num_end;
            continue;
        }

        result.append(line.substr(pos));
        break;
    }

    return result;
}

std::string TextSink::format_timestamp_us(std::uint64_t us_since_epoch)
{
    using namespace std::chrono;

    auto dur       = microseconds{us_since_epoch};
    auto tp        = system_clock::time_point{duration_cast<system_clock::duration>(dur)};
    auto time_t_tp = system_clock::to_time_t(tp);
    auto us        = us_since_epoch % 1'000'000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_tp), "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(6) << us;

    return oss.str();
}
```

#### Review & Status
- [sink_publisher.hpp — zgoda

To również jest sensowna zmiana.

TextSink::format_impl zawiera logikę runtime (parsowanie, konwersje).
format_timestamp_us wciąga ciężkie nagłówki (chrono, ctime, sstream, iomanip).
Metody nie są template.

Przeniesienie do .cpp:

odciąża header,
ogranicza zależności,
poprawia czas kompilacji.

Wniosek:
Zmiana uzasadniona, warto wdrożyć. ] Awaiting review

---

### ~~Change 3~~: `payload_register.hpp` — POMINIĘTA

**Decyzja:** Metody `debug_impl` są trywialne (2-linery), nadają się do inliningu, zero wpływu na compile time. Zmiana nie wnosi realnej wartości — pomijamy.

---

## Build Errors (if any)

---

## Test Results (if any)
