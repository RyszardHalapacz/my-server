# Proposal: Usunięcie ServerCRTP, spłaszczenie Server do flat class

**Date:** 2026-03-30

---

## Original Prompt

Całkowicie usuwamy ServerCRTP. Serwer będzie jedną klasą, nic po nic nie będzie dziedziczyć, a jego builder będzie go budował w runtime.

---

## Context

Codebase zawiera 4 warianty serwera. Zdecydowaliśmy, że `serwer3.h` jest najlepszy.
Teraz czyścimy architekturę: Server staje się flat class (zero dziedziczenia),
budowany przez Builder w runtime. ServerCRTP i SingleThreadServer odchodzą razem
z serwer2 i legacy metrics/serwer.

Klasa Server (z serwer3.h) dziedziczy `private metrics::MetricsMixin` — to też musi
zostać zamienione na kompozycję (member), żeby spełnić zasadę "nic po nic nie dziedziczy".

---

## Proposed Changes

### Change 1: `include/server/metrics/metrics.hpp` — uniezależnienie od server_crtp.hpp

**What:** Usunięcie `#include "server/server_crtp.hpp"`. Przeniesienie definicji `ServerMetrics` bezpośrednio do metrics.hpp, żeby plik był self-contained.
**Why:** server_crtp.hpp zostaje usunięty — metrics.hpp musi być niezależny.
**Impact:** Brak zmiany API. metrics_mixin_test.cpp dalej działa bez zmian.

**Proposed code:**
```cpp
#pragma once

#include <atomic>
#include <cstdint>

namespace server {

struct ServerMetrics {
    uint64_t accepted_total          = 0;
    uint64_t rejected_full_total     = 0;
    uint64_t rejected_stopped_total  = 0;
    uint64_t invalid_total           = 0;
    uint64_t error_total             = 0;

    uint64_t queue_depth_snapshot    = 0;
    uint64_t inflight_snapshot       = 0;
};

namespace metrics {

struct MetricsMixin {
    std::atomic<uint64_t> accepted_{0};
    std::atomic<uint64_t> rejected_full_{0};
    std::atomic<uint64_t> rejected_stopped_{0};
    std::atomic<uint64_t> invalid_{0};
    std::atomic<uint64_t> error_{0};
    std::atomic<uint64_t> inflight_{0};

    void onAccepted() noexcept { accepted_.fetch_add(1, std::memory_order_relaxed); }
    void onRejectedFull() noexcept { rejected_full_.fetch_add(1, std::memory_order_relaxed); }
    void onRejectedStopped() noexcept { rejected_stopped_.fetch_add(1, std::memory_order_relaxed); }
    void onInvalid() noexcept { invalid_.fetch_add(1, std::memory_order_relaxed); }
    void onError() noexcept { error_.fetch_add(1, std::memory_order_relaxed); }

    void inflightInc() noexcept { inflight_.fetch_add(1, std::memory_order_relaxed); }
    void inflightDec() noexcept { inflight_.fetch_sub(1, std::memory_order_relaxed); }

    [[nodiscard]] ServerMetrics snapshot(uint64_t queueDepth) const noexcept {
        ServerMetrics m;
        m.accepted_total          = accepted_.load(std::memory_order_relaxed);
        m.rejected_full_total     = rejected_full_.load(std::memory_order_relaxed);
        m.rejected_stopped_total  = rejected_stopped_.load(std::memory_order_relaxed);
        m.invalid_total           = invalid_.load(std::memory_order_relaxed);
        m.error_total             = error_.load(std::memory_order_relaxed);
        m.queue_depth_snapshot    = queueDepth;
        m.inflight_snapshot       = inflight_.load(std::memory_order_relaxed);
        return m;
    }
};

} // namespace metrics
} // namespace server
```

#### Review & Status
- [ ] Awaiting review

---

### Change 2: `include/server/serwer3.h` → rename to `include/server/server.h` — flat class, zero dziedziczenia

**What:**
1. Rename pliku `serwer3.h` → `server.h`
2. Usunięcie inline definicji `ServerMetrics` (linie 64-73) i `MetricsMixin` (linie 195-246) — zamiast tego `#include "server/metrics/metrics.hpp"`
3. `class Server final : private metrics::MetricsMixin` → `class Server final` (zero dziedziczenia)
4. Dodanie prywatnego membera `metrics::MetricsMixin metrics_{};`
5. Zamiana wszystkich `this->onXxx()` / `this->snapshot()` / `this->inflightXxx()` na `metrics_.xxx()`

**Why:** Decyzja architektoniczna — Server jest flat class, nie dziedziczy po niczym. MetricsMixin staje się składnikiem (composition over inheritance).
**Impact:** Brak zmiany publicznego API. Server nadal ma `metrics()`, `trySubmit()`, `start()`, `shutdown()`, `wait()`. Zmienia się tylko wewnętrzna struktura.

**Proposed code (kluczowe fragmenty zmian):**

Header — replace inline types with include:
```cpp
#include "event.hpp"
#include "server/metrics/metrics.hpp"
```

Usunięte sekcje (zastąpione include):
- Linie 43-73 (ServerState, ShutdownMode, SubmitStatus, ServerMetrics) — ServerMetrics przechodzi do metrics.hpp; enums `ServerState`, `ShutdownMode`, `SubmitStatus` pozostają w server.h (nie są w metrics)
- Linie 195-246 (cały `namespace metrics { struct MetricsMixin {...}; }`) — przechodzi do metrics.hpp

Deklaracja klasy:
```cpp
// BEFORE:
class Server final : private metrics::MetricsMixin {

// AFTER:
class Server final {
```

Nowy prywatny member (dodany do sekcji private members):
```cpp
    metrics::MetricsMixin metrics_{};
```

Zamiana wywołań (11 miejsc):
```cpp
// trySubmit():
this->onRejectedStopped()   →  metrics_.onRejectedStopped()
this->onInvalid()            →  metrics_.onInvalid()
this->onRejectedFull()       →  metrics_.onRejectedFull()   // 2x
this->onAccepted()           →  metrics_.onAccepted()

// metrics():
this->snapshot(...)          →  metrics_.snapshot(...)

// workerLoop():
this->inflightInc()          →  metrics_.inflightInc()
this->inflightDec()          →  metrics_.inflightDec()

// processEvent():
this->onError()              →  metrics_.onError()           // 3x
```

#### Review & Status
- [ ] Awaiting review

---

### Change 3: Usunięcie `include/server/server_crtp.hpp`

**What:** Plik usuwany w całości.
**Why:** ServerCRTP nie jest już potrzebny — Server jest flat class, nie dziedziczy.
**Impact:** Jedyni konsumenci to metrics.hpp (naprawiony w Change 1) i single_thread_server.hpp (usuwany w Change 4).

#### Review & Status
- [ ] Awaiting review

---

### Change 4: Usunięcie `include/server/single_thread_server.hpp`

**What:** Plik usuwany w całości.
**Why:** Zastąpiony przez nowy Server w server.h. SingleThreadServer dziedziczył z ServerCRTP — ten wzorzec odchodzi.
**Impact:** Usunięcie testów (Change 7), aktualizacja main.cpp (Change 6).

#### Review & Status
- [ ] Awaiting review

---

### Change 5: Usunięcie `include/server/serwer2.h` i `include/server/metrics/serwer`

**What:** Oba pliki usuwane w całości.
**Why:** Odrzucone warianty serwera. serwer2 — gorszy od serwer3. metrics/serwer — virtual interfaces, sprzeczne z filozofią projektu.
**Impact:** Usunięcie z CMakeLists.txt (Change 8).

#### Review & Status
- [ ] Awaiting review

---

### Change 6: `src/main.cpp` — nowy include

**What:** Zmiana include z `single_thread_server.hpp` na `server/server.h`.
**Why:** SingleThreadServer usunięty, nowy entry point to server.h.

**Proposed code:**
```cpp
#include "server/server.h"
#include "event.hpp"

int main()
{
}
```

#### Review & Status
- [ ] Awaiting review

---

### Change 7: Usunięcie testów SingleThreadServer

**What:** Usunięcie plików:
- `test/server/single_thread_server_test.cpp`
- `test/server/single_thread_server_extended_test.cpp`

**Why:** Testowały SingleThreadServer, który jest usuwany.
**Impact:** metrics_mixin_test.cpp zostaje — MetricsMixin dalej istnieje i jest testowany.

#### Review & Status
- [ ] Awaiting review

---

### Change 8: CMakeLists.txt — czyszczenie referencji

**What:**
- Root `CMakeLists.txt`: usunięcie `include/server/serwer2.h` z `add_executable(MyApp ...)`
- `test/CMakeLists.txt`: usunięcie 2 linii z testami SingleThreadServer

**Why:** Pliki usunięte — buildsystem musi to odzwierciedlać.

**Proposed code (root CMakeLists.txt, linia 29-30):**
```cmake
add_executable(MyApp src/main.cpp)
```

**Proposed code (test/CMakeLists.txt, usunięcie linii 28-29):**
```cmake
    # REMOVED: server/single_thread_server_test.cpp
    # REMOVED: server/single_thread_server_extended_test.cpp
    server/metrics_mixin_test.cpp
```

#### Review & Status
- [ ] Awaiting review

---

## Build Errors (if any)

### Build 1 (2026-03-30)
- `Event` struct is empty — no `port` / `payload` fields
- Example components (SqlParser, LocalExecutor, MysqlProtocolAdapter, TcpProtocolAdapter, ResultDistributor) referenced non-existent `ev.port` / `ev.payload`
- `validateEvent()` referenced `ev.payload.empty()`

**Fix:** Usunięcie wszystkich przykładowych komponentów. `validateEvent()` → zawsze `true` (stub do uzupełnienia gdy Event dostanie pola).

---

## Test Results (if any)

<populated after test run>
