# ✅ CLOSED
# Proposal: Usunięcie publisher/api/

**Date:** 2026-04-04

---

## Original Prompt

> tak (przeanalizuj moj kod, ja chyba nie potrzebuje PublishablePayload)

---

## Context

Analiza wykazała że cały katalog `include/publisher/api/` to martwy kod:

- `Publishable<Derived>` — nie jest includowany przez żaden plik produkcyjny ani testowy.
- `PublishablePayload<T>` — nie jest includowany przez nic. Dodatkowo niekompletny: `payload()` jest zakomentowany, więc `PublisherRuntime<Sink>::publish(obj)` i tak by nie skompilował.
- `LogEngine` używa `RegistrationHandle` bezpośrednio i woła `publish_view` z tokenem — całkowicie omija warstwę `api/`.

Jedyne referencje do tych plików:
1. `publishable_payload.hpp` includuje `publishable.hpp` (wewnętrzne)
2. `CMakeLists.txt:39` — `publishable.hpp` w liście źródeł `MyApp`, wyłącznie jako wpis IDE (header w `add_executable` nie jest kompilowany)

---

## Proposed Changes

### Change 1: usuń `include/publisher/api/publishable.hpp`

**What:** Usunięcie pliku.
**Why:** Zero użyć. Martwy kod.
**Impact:** Żaden plik produkcyjny ani testowy go nie includuje — brak wpływu na build.

#### Review & Status
- [ok ] Awaiting review

---

### Change 2: usuń `include/publisher/api/publishable_payload.hpp`

**What:** Usunięcie pliku.
**Why:** Zero użyć. Niekompletna implementacja (`payload()` zakomentowany). Martwy kod.
**Impact:** j.w.

#### Review & Status
- [ok ] Awaiting review

---

### Change 3: `CMakeLists.txt` — usuń wpis `include/publisher/api/publishable.hpp`

**What:** Usunięcie linii 39 z listy źródeł `add_executable(MyApp ...)`.
**Why:** Po usunięciu pliku wpis jest nieważny i spowoduje błąd CMake przy regeneracji.
**Impact:** Brak wpływu na kompilację — był to wpis wyłącznie dla IDE.

**Proposed code:**
```cmake
# Przed (linia 33-41):
add_executable(MyApp src/main.cpp
        include/publisher/core/publisher_types.hpp
        include/publisher/core/publish_token.hpp
        include/publisher/runtime/sink_handles.hpp
        include/publisher/runtime/token_registry.hpp
        include/publisher/runtime/registration_handle.hpp
        include/publisher/runtime/resource_store.hpp
        include/publisher/api/publishable.hpp          # <- REMOVE
        include/publisher/runtime/sink_traits.hpp
        include/publisher/runtime/publisher_runtime.hpp)

# Po:
add_executable(MyApp src/main.cpp
        include/publisher/core/publisher_types.hpp
        include/publisher/core/publish_token.hpp
        include/publisher/runtime/sink_handles.hpp
        include/publisher/runtime/token_registry.hpp
        include/publisher/runtime/registration_handle.hpp
        include/publisher/runtime/resource_store.hpp
        include/publisher/runtime/sink_traits.hpp
        include/publisher/runtime/publisher_runtime.hpp)
```

#### Review & Status
- [ok ] Awaiting review

---

## Build Errors (if any)

_brak — po zatwierdzeniu_

---

## Test Results (if any)

_brak — po zatwierdzeniu_
