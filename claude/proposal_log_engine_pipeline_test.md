# ✅ CLOSED
# Proposal: End-to-end pipeline test for LogEngine hot path

**Date:** 2026-03-29

---

## Original Prompt

> tak (punkt 6 — brak pokrycia hot path LogEngine end-to-end)

---

## Context

Żaden istniejący test nie weryfikuje rzeczywistego pipeline'u LogEngine:

- `full_pipeline_test` używa `TestLogEngine` — synchroniczny, omija kolejkę i worker całkowicie.
- Stress testy mają `submit_fn = [](void*) {}` — no-op, nic nie weryfikują co do submit.
- Zmiany w `submit_impl`, drain loop, recyklingu puli są niewidoczne dla test suite.

Potrzebujemy testów które przejdą przez: `enqueue` → `FreeList` → `MpscQueue` → `worker` → `submit_fn` → `destroy_fn`.

**Problem z inject submit_fn:** `SubmitFn = void(*)(void*)` — surowy wskaźnik, nie może przechwycić stanu. Rozwiązanie: `static std::atomic<uint64_t>` w pliku testowym + stateless lambda → poprawnie konwertowalna na function pointer.

---

## Proposed Changes

### Change 1a: nowy plik `test/logger/core/verifiable_engine.hpp`

**What:** `VerifiableEngine` + `PipelineEnvelope` + `g_submit_count` — analogicznie do `log_engine_stress.hpp`. Separacja infrastruktury od testów.

**Proposed code:**

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <thread>

#include "logger/core/log_record.hpp"
#include "logger/core/lockfree_queue.hpp"

using logger::core::detail::FreeList;
using logger::core::detail::FreeNode;
using logger::core::detail::MpscQueue;
using logger::core::detail::MpscNode;
using logger::core::detail::LogRecord;

// Global submit counter — required because SubmitFn is a raw function pointer
// (no captures). Reset to 0 before each test.
inline std::atomic<uint64_t> g_submit_count{0};

class VerifiableEngine
{
public:
    explicit VerifiableEngine(std::size_t pool_size = 256)
        : pool_size_(pool_size)
        , pool_(std::make_unique<LogRecord[]>(pool_size))
    {
        for (std::size_t i = 0; i < pool_size_; ++i)
            freelist_.push(&pool_[i]);
    }

    ~VerifiableEngine() { shutdown(); }

    VerifiableEngine(const VerifiableEngine&) = delete;
    VerifiableEngine& operator=(const VerifiableEngine&) = delete;

    void start()
    {
        bool expected = false;
        if (run_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            worker_ = std::thread(&VerifiableEngine::worker_loop, this);
    }

    template <typename Envelope>
    bool enqueue(Envelope&& env)
    {
        LogRecord* rec = static_cast<LogRecord*>(freelist_.try_pop());
        if (!rec) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        using E = std::decay_t<Envelope>;
        struct Stored { E env; };
        static_assert(sizeof(Stored) <= LogRecord::StorageSize);
        static_assert(alignof(Stored) <= LogRecord::StorageAlign);

        new (rec->storage_ptr()) Stored{std::forward<Envelope>(env)};

        rec->submit_fn  = [](void*) {
            g_submit_count.fetch_add(1, std::memory_order_relaxed);
        };
        rec->destroy_fn = [](void* s) noexcept {
            static_cast<Stored*>(s)->~Stored();
        };

        queue_.push(rec);
        enqueued_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void shutdown() noexcept
    {
        bool expected = true;
        if (run_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
            if (worker_.joinable())
                worker_.join();
    }

    uint64_t enqueued() const noexcept { return enqueued_.load(std::memory_order_relaxed); }
    uint64_t written()  const noexcept { return written_.load(std::memory_order_relaxed); }
    uint64_t dropped()  const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    void worker_loop()
    {
        using namespace std::chrono_literals;
        LogRecord* pending_recycle = nullptr;

        while (run_.load(std::memory_order_acquire) || !queue_.empty()) {
            MpscNode* node = queue_.pop();
            if (!node) { std::this_thread::sleep_for(50us); continue; }
            LogRecord* rec = static_cast<LogRecord*>(node);

            if (pending_recycle) freelist_.push(pending_recycle);

            rec->submit_fn(rec->storage_ptr());
            rec->destroy_fn(rec->storage_ptr());
            written_.fetch_add(1, std::memory_order_relaxed);
            pending_recycle = rec;
        }

        MpscNode* node = nullptr;
        while ((node = queue_.pop()) != nullptr) {
            LogRecord* rec = static_cast<LogRecord*>(node);
            if (pending_recycle) freelist_.push(pending_recycle);

            rec->submit_fn(rec->storage_ptr());
            rec->destroy_fn(rec->storage_ptr());
            written_.fetch_add(1, std::memory_order_relaxed);
            pending_recycle = rec;
        }
    }

    std::size_t pool_size_;
    std::unique_ptr<LogRecord[]> pool_;
    FreeList  freelist_;
    MpscQueue queue_;
    std::atomic<bool>     run_{false};
    std::thread           worker_;
    std::atomic<uint64_t> enqueued_{0};
    std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> dropped_{0};
};

struct PipelineEnvelope { uint64_t id; };
static_assert(sizeof(PipelineEnvelope) <= LogRecord::StorageSize);
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 1b: nowy plik `test/logger/core/log_engine_pipeline_test.cpp`

**What:** Tylko testy — includuje `verifiable_engine.hpp`. Cztery testy pokrywające hot path i drain loop.

**Why:** Wypełnia lukę w pokryciu — zmiany w `submit_impl`, drain loop i recyklingu puli będą od teraz weryfikowane.

**Impact:** Cold path (testy). Zero wpływu na produkcyjny kod.

**Proposed code:**

```cpp
#include <gtest/gtest.h>
#include "logger/core/verifiable_engine.hpp"

// submit_fn is called for every enqueued record
TEST(LogEnginePipeline, SubmitFnCalledForEveryRecord)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr uint64_t N = 100;

    VerifiableEngine engine;
    engine.start();

    for (uint64_t i = 0; i < N; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();

    EXPECT_EQ(engine.enqueued(), N);
    EXPECT_EQ(engine.written(),  N);
    EXPECT_EQ(engine.dropped(),  0u);
    EXPECT_EQ(g_submit_count.load(), N);
}

// Records enqueued just before shutdown are processed by drain loop
TEST(LogEnginePipeline, DrainLoopProcessesAllPendingRecords)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr uint64_t N = 50;

    VerifiableEngine engine;
    engine.start();

    for (uint64_t i = 0; i < N; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();  // immediate — records may still be in queue

    EXPECT_EQ(engine.written(), engine.enqueued());
    EXPECT_EQ(g_submit_count.load(), engine.enqueued());
}

// Pool exhaustion — excess records are dropped, not corrupted
TEST(LogEnginePipeline, DropsWhenPoolExhausted)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr std::size_t pool_size = 16;
    constexpr uint64_t    enqueue_count = 64;

    VerifiableEngine engine{pool_size};
    // do NOT start worker — pool won't be recycled
    // so after pool_size enqueues, rest must be dropped

    for (uint64_t i = 0; i < enqueue_count; ++i)
        engine.enqueue(PipelineEnvelope{i});

    EXPECT_EQ(engine.enqueued(), pool_size);
    EXPECT_EQ(engine.dropped(),  enqueue_count - pool_size);
}

// Pool is recycled — after processing, new records can be enqueued without drops
TEST(LogEnginePipeline, PoolRecycledAfterProcessing)
{
    g_submit_count.store(0, std::memory_order_relaxed);
    constexpr std::size_t pool_size = 32;

    VerifiableEngine engine{pool_size};
    engine.start();

    // First batch — fill pool
    for (std::size_t i = 0; i < pool_size; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();
    ASSERT_EQ(engine.dropped(), 0u);

    // Restart and enqueue again — pool must have been recycled
    engine.start();
    for (std::size_t i = 0; i < pool_size; ++i)
        engine.enqueue(PipelineEnvelope{i});

    engine.shutdown();

    EXPECT_EQ(engine.dropped(), 0u);
    EXPECT_EQ(g_submit_count.load(), pool_size * 2);
}
```

#### Review & Status
- [ ok, ale sepracja samych testow od VerifiableEngine] Awaiting review

---

### Change 2: `test/CMakeLists.txt` — dodaj nowy plik

**What:** Dodać `logger/core/log_engine_pipeline_test.cpp` do listy źródeł `main_test`.

**Proposed code:**
```cmake
logger/core/log_engine_pipeline_test.cpp
```
(po `logger/core/mpsc_queue_test.cpp`)

#### Review & Status
- [ ok, ale sepracja samych testow od VerifiableEngine] Awaiting review

---

---

### Change 3: `include/logger/core/lockfree_queue.hpp` — dodaj `MpscQueue::reset()`

**What:** Nowa metoda `reset()` przywraca `stub_` jako dummy node.

**Why:** Po opróżnieniu kolejki `pop()` zwraca ostatni węzeł `R` i robi `head_ = R`. Tym samym `R` jest jednocześnie `head_` (dummy) i `pending_recycle`. Gdy worker oddaje `R` do freelisty, a potem producentka go ponownie enqueueuje, `push()` wykonuje `tail_.exchange(R)` zwracające `R` (bo `tail_ == R`), a następnie `R->next = R` — pętla na sobie. Kolejne `pop()` zwraca `R` jako dane, nie advancing `head_`, co powoduje utratę jednego rekordu.

`reset()` przywraca `stub_` jako dummy zanim `pending_recycle` trafi do freelisty, zerując ryzyko.

**Impact:** Cold path (wywoływane raz przy shutdown). Brak wpływu na hot path.

**Proposed code:**
```cpp
// Dodać do klasy MpscQueue, po metodzie empty():
void reset() noexcept
{
    stub_.next.store(nullptr, std::memory_order_relaxed);
    head_ = &stub_;
    tail_.store(&stub_, std::memory_order_relaxed);
}
```

#### Review & Status
- [ ok] Awaiting review

---

### Change 4: `test/logger/core/verifiable_engine.hpp` — `queue_.reset()` przed recyclingiem

**What:** Wywołanie `queue_.reset()` na końcu `worker_loop`, przed `freelist_.push(pending_recycle)`.

**Why:** Bezpośrednio stosuje Fix z Change 3. Bez tego `PoolRecycledAfterProcessing` traci jeden rekord przy każdym restarcie.

**Proposed code:**
```cpp
// Zastąpić koniec worker_loop (po obu pętlach drain):
        queue_.reset();
        if (pending_recycle)
            freelist_.push(pending_recycle);
```

#### Review & Status
- [ok] Awaiting review

---

### Change 5: `include/logger/core/log_engine.hpp` — `queue_.reset()` przed recyclingiem

**What:** To samo co Change 4 — `queue_.reset()` przed finalnym `freelist_.push(pending_recycle)` w `worker_loop`.

**Proposed code:**
```cpp
// Zastąpić koniec worker_loop:
        queue_.reset();
        if (pending_recycle)
            freelist_.push(pending_recycle);
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 6: `test/stress/log_engine_stress.hpp` — `queue_.reset()` przed recyclingiem

**What:** To samo co Change 4/5 dla `StressableLogEngine::worker_loop`.

#### Review & Status
- [ ok] Awaiting review

---

## Build Errors (if any)

_Brak — do uzupełnienia po buildzie._

---

## Test Results (if any)

Pierwsze uruchomienie po pierwszym fiksie (pending_recycle push po drain):
- `PoolRecycledAfterProcessing` FAILED: dropped=0, g_submit_count=63 (oczekiwano 64)
- Pozostałe 3 testy PASSED
