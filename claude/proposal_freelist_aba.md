# ✅ CLOSED — ACCEPTED RISK (no fix applied)
# Proposal: Fix ABA in FreeList via 128-bit tagged pointer

**Date:** 2026-03-29

---

## Original Prompt

> tak (przepisz proposal na wersję ze struct zamiast manipulacji bitami)

---

## Context

`FreeList` to Treiber stack z `std::atomic<FreeNode*>`. Klasyczny scenariusz ABA:

```
FreeList: head → [A] → [B] → [C]

P1: czyta head=A, next=B  ← PREEMPTED
P2: pop A, używa, push do MpscQueue
P2: pop B, używa, push do MpscQueue
Worker: przetwarza A, pushuje A z powrotem do FreeList
P1: CAS(A → B) succeeds  ← ABA!
→ head_ = B, ale B jest aktywnie w użyciu przez P2
→ P3 woła try_pop(), dostaje B — dwa wątki mają ten sam węzeł
```

**Rozwiązanie:** `std::atomic<TaggedPtr>` — struct z dwoma polami: wskaźnik + licznik wersji. CAS porównuje **oba naraz** (128-bit `cmpxchg16b` na x86-64). Każda operacja bumping tag — nawet jeśli wskaźnik wróci pod ten sam adres, tag będzie inny i CAS odpada.

Zero manipulacji bitami. Tag to zwykłe pole `uintptr_t`.

---

## Proposed Changes

### Change 1: `include/logger/core/lockfree_queue.hpp` — przepisz `FreeList` na `std::atomic<TaggedPtr>`

**What:**
- Dodać `struct alignas(16) TaggedPtr` z polami `ptr` i `tag`.
- `head_` zmienia typ z `std::atomic<FreeNode*>` na `std::atomic<TaggedPtr>`.
- `static_assert` że atomic jest lock-free (gwarantuje `cmpxchg16b`, nie mutex).
- Publiczny interfejs (`push`/`try_pop`/`empty`) bez zmian — zero zmian poza klasą.

**Why:** Eliminuje ABA bez jakiejkolwiek manipulacji bitami. Tag jest osobnym polem — czytelny, oczywisty.

**Impact:** `head_` rośnie z 8B do 16B. Operacja CAS to `cmpxchg16b` zamiast `cmpxchg` — minimalnie wolniejszy, ale FreeList nie jest na hot path (hot path to `MpscQueue`). Zero zmian w testach i reszcie kodu.

**Proposed code** (tylko klasa `FreeList` — `MpscQueue` bez zmian):

```cpp
class FreeList
{
    struct alignas(16) TaggedPtr
    {
        FreeNode* ptr{nullptr};
        uintptr_t tag{0};
    };

    static_assert(std::atomic<TaggedPtr>::is_always_lock_free,
                  "FreeList requires lock-free 128-bit CAS (cmpxchg16b)");

    std::atomic<TaggedPtr> head_{};

public:
    FreeList() = default;

    void push(FreeNode* n) noexcept
    {
        TaggedPtr h = head_.load(std::memory_order_relaxed);
        TaggedPtr new_head;
        do
        {
            n->free_next = h.ptr;
            new_head = {n, h.tag + 1};
        } while (!head_.compare_exchange_weak(h, new_head,
                                              std::memory_order_release,
                                              std::memory_order_relaxed));
    }

    FreeNode* try_pop() noexcept
    {
        TaggedPtr h = head_.load(std::memory_order_acquire);
        while (h.ptr)
        {
            TaggedPtr new_head = {h.ptr->free_next, h.tag + 1};
            if (head_.compare_exchange_weak(h, new_head,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            {
                return h.ptr;
            }
        }
        return nullptr;
    }

    bool empty() const noexcept
    {
        return head_.load(std::memory_order_acquire).ptr == nullptr;
    }
};
```

#### Review & Status
- [ok ] Awaiting review

---

## Build Errors (if any)

_Brak — do uzupełnienia po buildzie._

---

## Test Results (if any)

_Brak — do uzupełnienia po uruchomieniu testów._
