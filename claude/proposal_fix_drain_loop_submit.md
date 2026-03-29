# ✅ CLOSED
# Proposal: Fix drain loop — missing submit_fn call at shutdown

**Date:** 2026-03-29

---

## Original Prompt

> zapisz ta liste, zajmiemy sie nimi pokolei teraz daj mi teraz 1 punkt proposal

---

## Context

`LogEngine::worker_loop` ma dwie pętle:

1. **Główna pętla** (`while (run_ || !queue_.empty())`) — przetwarza rekordy w normalnym trybie.
2. **Drain loop** (po ustawieniu `run_ = false`) — ma na celu opróżnienie kolejki przy shutdown.

Drain loop wywołuje `process_fn` i `destroy_fn`, ale **pomija `submit_fn`**. W efekcie wszystkie rekordy, które były w kolejce w momencie wywołania `shutdown()`, są niszczone bez publikacji — ciche utracenie danych.

Dodatkowo: w drain loop `oss.str()` jest wypisywane przez `std::cout`, co jest niespójne z resztą systemu (główna pętla używa `submit_fn` → `Publisher`).

---

## Proposed Changes

### Change 1: `include/logger/core/log_engine.hpp` — napraw drain loop

**What:** Dodaj wywołanie `rec->submit_fn(rec->storage_ptr())` w drain loop, identycznie jak w głównej pętli. Usuń `std::ostringstream oss` i `std::cout << oss.str()` — są niespójne i niepotrzebne (process_fn jest i tak pusty).

**Why:** Drain loop ma opróżnić kolejkę i opublikować wszystko przed zamknięciem — bez wywołania `submit_fn` rekordy są po cichu gubione. `std::cout` w drain loop to artefakt debugowania niespójny z architekturą Publishera.

**Impact:** Brak wpływu na wydajność (cold path). Naprawia korektność przy shutdown. Zerowe ryzyko architektoniczne.

**Proposed code:**

Przed (linie ~196-212):
```cpp
LogRecord *rec = nullptr;
while ((rec = queue_.pop()) != nullptr)
{
    if (pending_recycle)
    {
        freelist_.push(pending_recycle);
    }

    std::ostringstream oss;
    rec->process_fn(rec->storage_ptr(), oss);
    std::cout << oss.str() << '\n';

    rec->destroy_fn(rec->storage_ptr());
    written_.fetch_add(1, std::memory_order_relaxed);

    pending_recycle = rec;
}
```

Po:
```cpp
LogRecord *rec = nullptr;
while ((rec = queue_.pop()) != nullptr)
{
    if (pending_recycle)
    {
        freelist_.push(pending_recycle);
    }

    rec->submit_fn(rec->storage_ptr());
    rec->destroy_fn(rec->storage_ptr());
    written_.fetch_add(1, std::memory_order_relaxed);

    pending_recycle = rec;
}
```

#### Review & Status
- [ ok] Awaiting review

---

## Build Errors (if any)

_Brak — do uzupełnienia po buildzie._

---

## Test Results (if any)

_Brak — do uzupełnienia po uruchomieniu testów._
