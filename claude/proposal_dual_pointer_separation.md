# ✅ CLOSED
# Proposal: Separate MpscNode / FreeNode base types for LogRecord

**Date:** 2026-03-29

---

## Original Prompt

> nie kulbie friendow, zastanawiam sie nad druga opcja (opcja B z listy LogRecord issues)

---

## Context

`LogRecord` ma dwa wskaźniki do linked list: `next` (dla `MpscQueue`) i `free_next` (dla `FreeList`). Obydwa są publiczne — nic nie stoi na przeszkodzie, żeby użyć `free_next` w `MpscQueue` lub odwrotnie. Błąd byłby cichy i katastrofalny.

Rozwiązanie: dwa oddzielne base structs — `MpscNode` i `FreeNode`. `LogRecord` dziedziczy z obu. Każda kolejka operuje wyłącznie na swoim typie węzła — kompilator egzekwuje separację.

**Dodatkowy zysk:** `MpscQueue::stub_` (sentinel) to teraz `MpscNode` (8B) zamiast pełnego `LogRecord` (384B).

**Pułapka przy castach:**
- `MpscNode` jest pierwszą bazą → `MpscNode*` == `LogRecord*` (ten sam adres)
- `FreeNode` jest drugą bazą → `FreeNode*` = `LogRecord* + 8`
- Dlatego przy odzyskiwaniu `LogRecord*` z `FreeList` obowiązuje `static_cast` (nie `reinterpret_cast`).

---

## Proposed Changes

### Change 1: `include/logger/core/log_record.hpp` — dodaj `MpscNode`, `FreeNode`, zaktualizuj `LogRecord`

**What:** Zdefiniować dwa nowe structs przed `LogRecord`. `LogRecord` dziedziczy z obu i traci bezpośrednie deklaracje `next` i `free_next`.

**Why:** Typy węzłów stają się odrębnymi bytami — `MpscQueue` widzi tylko `MpscNode*`, `FreeList` widzi tylko `FreeNode*`. Pomyłka między nimi to błąd kompilacji.

**Impact:** Zero wpływu na runtime — layout jest identyczny (te same pola, ten sam rozmiar). Czysto strukturalna zmiana.

**Proposed code:**
```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <cstddef>

namespace logger::core::detail
{
    struct MpscNode
    {
        std::atomic<MpscNode *> next{nullptr};
    };

    struct FreeNode
    {
        FreeNode *free_next{nullptr};
    };

    struct alignas(64) LogRecord : MpscNode, FreeNode
    {
        static constexpr std::size_t StorageSize = 256;
        static constexpr std::size_t StorageAlign = 64;

        alignas(StorageAlign) unsigned char storage[StorageSize];

        using DestroyFn = void (*)(void *storage);
        using SubmitFn  = void (*)(void *storage);

        DestroyFn destroy_fn{nullptr};
        SubmitFn  submit_fn{nullptr};

        void *storage_ptr() noexcept { return static_cast<void *>(storage); }
    };
}
```

#### Review & Status
-[ok ] Awaiting review

---

### Change 2: `include/logger/core/lockfree_queue.hpp` — `FreeList` → `FreeNode`, `MpscQueue` → `MpscNode`

**What:**
- Usunąć `using Node = LogRecord`.
- `FreeList`: wszystkie `Node*` zastąpić `FreeNode*`.
- `MpscQueue`: wszystkie `Node*` zastąpić `MpscNode*`, `Node stub_` → `MpscNode stub_`.

**Why:** Kolejki nie powinny wiedzieć o `LogRecord` — operują wyłącznie na swoim typie węzła. `stub_` zmniejsza się z 384B do 8B.

**Impact:** `stub_` oszczędza 376B. Typy zwracane z `pop()`/`try_pop()` to `MpscNode*`/`FreeNode*` — caller musi wykonać `static_cast<LogRecord*>`, co jest jawnym i bezpiecznym sygnałem przywracania pełnego typu.

**Proposed code:**
```cpp
#pragma once
#include "log_record.hpp"

namespace logger::core::detail
{
    class FreeList
    {
        std::atomic<FreeNode *> head_{nullptr};

    public:
        FreeList() = default;

        void push(FreeNode *n) noexcept
        {
            FreeNode *h = head_.load(std::memory_order_relaxed);
            do
            {
                n->free_next = h;
            } while (!head_.compare_exchange_weak(h, n,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed));
        }

        FreeNode *try_pop() noexcept
        {
            FreeNode *h = head_.load(std::memory_order_acquire);
            while (h)
            {
                FreeNode *next = h->free_next;
                if (head_.compare_exchange_weak(h, next,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire))
                {
                    return h;
                }
            }
            return nullptr;
        }

        bool empty() const noexcept
        {
            return head_.load(std::memory_order_acquire) == nullptr;
        }
    };

    class MpscQueue
    {
        std::atomic<MpscNode *> tail_;
        MpscNode *head_;
        MpscNode stub_;

    public:
        MpscQueue()
        {
            stub_.next.store(nullptr, std::memory_order_relaxed);
            head_ = &stub_;
            tail_.store(&stub_, std::memory_order_relaxed);
        }

        void push(MpscNode *n) noexcept
        {
            n->next.store(nullptr, std::memory_order_relaxed);
            MpscNode *prev = tail_.exchange(n, std::memory_order_acq_rel);
            prev->next.store(n, std::memory_order_release);
        }

        MpscNode *pop() noexcept
        {
            MpscNode *head = head_;
            MpscNode *next = head->next.load(std::memory_order_acquire);
            if (!next)
            {
                if (tail_.load(std::memory_order_acquire) == head)
                    return nullptr;
                do
                {
                    next = head->next.load(std::memory_order_acquire);
                } while (!next);
            }
            head_ = next;
            return next;
        }

        bool empty() const noexcept
        {
            MpscNode *head = head_;
            MpscNode *next = head->next.load(std::memory_order_acquire);
            if (next)
                return false;
            return tail_.load(std::memory_order_acquire) == head;
        }
    };
}
```

#### Review & Status
-[ok ] Awaiting review

---

### Change 3: `include/logger/core/log_engine.hpp` — dodaj casty przy acquire i worker_loop

**What:** Dwa miejsca wymagają jawnego `static_cast<LogRecord*>`:
- `acquire_record()` — `FreeNode*` → `LogRecord*`
- `worker_loop()` — `MpscNode*` → `LogRecord*` (w obu pętlach)

`push_to_queue` i `freelist_.push(pending_recycle)` nie wymagają zmian — implicit upcast `LogRecord*` → `MpscNode*`/`FreeNode*` jest bezpieczny.

**Why:** `FreeNode` jest drugą bazą (offset +8), więc `static_cast` jest obowiązkowy. Jawność casta komunikuje czytelnikowi: "tu odzyskujemy pełny typ z węzła kolejki".

**Proposed code:**

`acquire_record`:
```cpp
LogRecord *acquire_record()
{
    FreeNode *node = freelist_.try_pop();
    return node ? static_cast<LogRecord *>(node) : nullptr;
}
```

`worker_loop` (obie pętle — ten sam wzorzec):
```cpp
MpscNode *node = queue_.pop();
if (!node)
{
    std::this_thread::sleep_for(50us);
    continue;
}
LogRecord *rec = static_cast<LogRecord *>(node);
```

#### Review & Status
-[ok ] Awaiting review

---

### Change 4: `test/logger/core/freelist_test.cpp` — cast wyników `try_pop()`

**What:** `fl.try_pop()` zwraca teraz `FreeNode*`. Wszystkie wywołania owinąć `static_cast<LogRecord*>(...)`. Zmienić `std::set<LogRecord*>` żeby korzystał z rzutowanego wyniku.

**Why:** Bez casta `std::set<LogRecord*>` nie skompiluje się (brak implicit `FreeNode*` → `LogRecord*`).

**Proposed code** (fragmenty):
```cpp
// PushPopSingle
auto* popped = static_cast<LogRecord*>(fl.try_pop());
EXPECT_EQ(popped, &node);

// LIFOOrdering
EXPECT_EQ(static_cast<LogRecord*>(fl.try_pop()), &c);
EXPECT_EQ(static_cast<LogRecord*>(fl.try_pop()), &b);
EXPECT_EQ(static_cast<LogRecord*>(fl.try_pop()), &a);

// PushNPopNAllReturned
std::set<LogRecord*> popped;
for (int i = 0; i < N; ++i) {
    auto* p = static_cast<LogRecord*>(fl.try_pop());
    ASSERT_NE(p, nullptr);
    popped.insert(p);
}
```

#### Review & Status
-[ok ] Awaiting review

---

### Change 5: `test/logger/core/mpsc_queue_test.cpp` — cast wyników `pop()`

**What:** `q.pop()` zwraca teraz `MpscNode*`. Wszystkie wywołania owinąć `static_cast<LogRecord*>(...)`.

**Why:** Spójność z resztą kodu. `MpscNode` jest pierwszą bazą (ten sam adres), więc cast jest no-op na poziomie runtime — ale konieczny dla poprawności typów.

**Proposed code** (fragmenty):
```cpp
// PushPopSingle
auto* popped = static_cast<LogRecord*>(q.pop());
EXPECT_EQ(popped, &node);

// FIFOOrdering
EXPECT_EQ(static_cast<LogRecord*>(q.pop()), &a);
EXPECT_EQ(static_cast<LogRecord*>(q.pop()), &b);
EXPECT_EQ(static_cast<LogRecord*>(q.pop()), &c);

// PushNPopNOrdered
auto* p = static_cast<LogRecord*>(q.pop());
ASSERT_NE(p, nullptr);
EXPECT_EQ(p, &nodes[i]);
```

#### Review & Status
-[ok ] Awaiting review

---

### Change 6: `test/stress/log_engine_stress.hpp` — cast w `worker_loop`

**What:** `queue_.pop()` zwraca `MpscNode*`. Dodać `static_cast<LogRecord*>` w obu pętlach worker_loop.

**Proposed code:**
```cpp
MpscNode *node = queue_.pop();
if (!node) {
    std::this_thread::sleep_for(50us);
    continue;
}
LogRecord *rec = static_cast<LogRecord *>(node);
```

#### Review & Status
-[ok ] Awaiting review

---

## Build Errors (if any)

_Brak — do uzupełnienia po buildzie._

---

## Test Results (if any)

_Brak — do uzupełnienia po uruchomieniu testów._
