# ✅ CLOSED
# Proposal: Fix minor issues in sink_traits.hpp and publisher_runtime.hpp

**Date:** 2026-04-04

---

## Original Prompt

Code review nowych plików — dwie drobne poprawki.

---

## Proposed Changes

### Change 1: `include/publisher/runtime/publisher_runtime.hpp` — komentarz przy primary template

**What:** Dodanie komentarza `// Primary template — intentionally undefined.`

**Why:** Bez komentarza wygląda jak niekompletny kod, nie celowy design. Każdy czytający powinien wiedzieć, że brak definicji jest zamierzony — nieznany `SinkKind` ma dawać błąd kompilacji.

**Proposed code:**
```cpp
    // Primary template — intentionally undefined.
    // Passing an unregistered SinkKind is a compile-time error.
    template<publisher::core::SinkKind Sink>
    struct PublisherRuntime;
```

#### Review & Status
- - [ ok] Awaiting review

---

### Change 2: Brak newline na końcu `sink_traits.hpp` i `publisher_runtime.hpp`

**What:** Dodanie `\n` na końcu obu plików.

**Why:** Clang ostrzega: *"no newline at end of file"*. POSIX wymaga `\n` na końcu pliku tekstowego.

**Impact:** Zero.

#### Review & Status
- - [ ok] Awaiting review

---

## Build Errors (if any)

_Populated after build._

---

## Test Results (if any)

_Populated after tests._
