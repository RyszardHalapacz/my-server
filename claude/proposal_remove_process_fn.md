# ✅ CLOSED
# Proposal: Remove dead process_fn machinery

**Date:** 2026-03-29

---

## Original Prompt

> tak (przygotuj proposal dla punktu 2 z listy LogRecord issues)

---

## Context

`process_fn` / `process_impl` to całkowicie martwy mechanizm:

- `process_impl` ma puste ciało — nie robi nic z przekazanym `storage` ani `os`.
- `worker_loop` tworzy `std::ostringstream oss` na każdej iteracji, przekazuje do `process_fn`, ale wynik nigdy nie jest użyty.
- `ProcessFn` typedef i pole `process_fn` w `LogRecord` zajmują miejsce w strukturze bez żadnego efektu.

Cały pipeline faktycznie robi: `submit_fn` → `destroy_fn`. Wywołanie `process_fn` + budowanie `oss` to czysty overhead bez wartości.

---

## Proposed Changes

### Change 1: `include/logger/core/log_record.hpp` — usuń `ProcessFn` i `process_fn`

**What:** Usunąć typedef `ProcessFn` i pole `process_fn`.

**Why:** Pole nigdy nie wykonuje żadnej pracy. Jego obecność wprowadza w błąd czytelnika kodu i zajmuje miejsce w każdym `LogRecord` (8B na polu + overhead w cache line funkcyjnych pointerów).

**Impact:** Zmniejszenie rozmiaru `LogRecord` o 8B. Zero wpływu na logikę.

**Proposed code:**
```cpp
struct alignas(64) LogRecord
{
    std::atomic<LogRecord *> next{nullptr};
    LogRecord *free_next{nullptr};

    static constexpr std::size_t StorageSize = 256;
    static constexpr std::size_t StorageAlign = 64;

    alignas(StorageAlign) unsigned char storage[StorageSize];

    using DestroyFn = void (*)(void *storage);
    using SubmitFn  = void (*)(void *storage);

    DestroyFn destroy_fn{nullptr};
    SubmitFn  submit_fn{nullptr};

    void *storage_ptr() noexcept { return static_cast<void *>(storage); }
};
```

#### Review & Status
- [ ok] Awaiting review

---

### Change 2: `include/logger/core/log_engine.hpp` — usuń `process_impl`, przypisanie i `oss` w worker loop

**What:** Trzy usunięcia:
1. Metoda `process_impl` (linie 121–131).
2. Przypisanie `rec->process_fn = &process_impl<Stored>` w `enqueue` (linia 58).
3. `std::ostringstream oss` + `rec->process_fn(...)` w głównej pętli `worker_loop` (linie 187–188).
4. Usunięcie `#include <ostream>` jeśli nie jest używany gdzie indziej.

**Why:** Po usunięciu pola z `LogRecord` kod nie skompiluje się bez tych usunięć. Samodzielnie: `oss` tworzony na każdej iteracji hot path to niepotrzebna alokacja heapowa bez efektu.

**Impact:** Jedna alokacja heapowa mniej na każdy przetworzony rekord w głównej pętli. Czystszy, mniejszy kod.

**Proposed code:**

`enqueue` — przed:
```cpp
rec->process_fn = &process_impl<Stored>;
rec->destroy_fn = &destroy_impl<Stored>;
rec->submit_fn  = &submit_impl<Stored>;
```

`enqueue` — po:
```cpp
rec->destroy_fn = &destroy_impl<Stored>;
rec->submit_fn  = &submit_impl<Stored>;
```

`worker_loop` główna pętla — przed:
```cpp
std::ostringstream oss;
rec->process_fn(rec->storage_ptr(), oss);
rec->submit_fn(rec->storage_ptr());
rec->destroy_fn(rec->storage_ptr());
```

`worker_loop` główna pętla — po:
```cpp
rec->submit_fn(rec->storage_ptr());
rec->destroy_fn(rec->storage_ptr());
```

Cała metoda `process_impl` do usunięcia:
```cpp
// type-erased processing: invoke the method on the envelope
template <typename Stored>
static void process_impl(void *storage, std::ostream &os)
{
    auto *obj = static_cast<Stored *>(storage);
    auto &env = obj->env;
}
```

#### Review & Status
- [ok ] Awaiting review

---

## Build Errors (if any)

_Brak — do uzupełnienia po buildzie._

---

## Test Results (if any)

_Brak — do uzupełnienia po uruchomieniu testów._
