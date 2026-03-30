# ✅ CLOSED
# Proposal: Remove legacy ServerBase hierarchy and handlers

**Date:** 2026-03-30

---

## Original Prompt

Usuń wszystko co związane z klasą ServerBase, wszystkie dziedziczące po niej handlery, i testy.

---

## Context

`ServerBase` i jej pochodne (`ServerConditionVar`, `ServerThreaded`, `ServerAsync`, `ServerSingleThread`) to legacy kod oznaczony jako `[[deprecated]]`. Zależą od `IDatabaseHandler`, `ConditionVariableDatabaseHandler` i `ThreadedDatabaseHandler` — klas z wieloma bugami (data races, deadlock, virtual dispatch). Projekt przeszedł na `server::SingleThreadServer` (CRTP). Czas wyczyścić martwy kod.

---

## Proposed Changes

### Change 1: `include/server/server.hpp` — usunięcie pliku

**What:** Usunięcie całego pliku. Zawiera `ServerBase<T>`, `ServerBase<void>`, `ServerConditionVar`, `ServerThreaded`, `ServerAsync`, `ServerSingleThread`.
**Why:** Cały plik to deprecated legacy — zastąpiony przez `SingleThreadServer`.
**Impact:** Niski risk — wszystko oznaczone `[[deprecated]]`, nowy serwer jest niezależny.

#### Review & Status
- [ ok] Awaiting review

---

### Change 2: `include/handler/database_handler_interface.hpp` — usunięcie pliku

**What:** Usunięcie interfejsu `IDatabaseHandler` (virtual base class).
**Why:** Używany wyłącznie przez usuwane handlery. Sprzeczny z filozofią projektu (no virtual dispatch).
**Impact:** Brak — żaden aktywny kod go nie używa.

#### Review & Status
- [ ok] Awaiting review

---

### Change 3: `include/handler/condition_variable_database_handler.hpp` + `src/handler/condition_variable_database_handler.cpp` — usunięcie plików

**What:** Usunięcie headera i implementacji `ConditionVariableDatabaseHandler`.
**Why:** Legacy handler z deadlockiem w destruktorze, data race na `is_active_`, itd. Używany tylko przez usuwany `ServerConditionVar`.
**Impact:** Brak — żaden aktywny kod go nie używa.

#### Review & Status
- [ ok] Awaiting review

---

### Change 4: `include/handler/threaded_database_handler.hpp` + `src/handler/threaded_database_handler.cpp` — usunięcie plików

**What:** Usunięcie headera i implementacji `ThreadedDatabaseHandler`.
**Why:** Legacy handler z busy-wait polling, data race. Używany tylko przez usuwany `ServerThreaded`.
**Impact:** Brak — żaden aktywny kod go nie używa.

#### Review & Status
- [ ok] Awaiting review

---

### Change 5: `src/server/server.cpp` — usunięcie pliku

**What:** Usunięcie pliku. Zawiera wyłącznie zakomentowany kod starego `Server`.
**Why:** Martwy kod, cały zakomentowany.
**Impact:** Brak.

#### Review & Status
- [ ok] Awaiting review

---

### Change 6: `test/handler/condition_variable_handler_test.cpp` — usunięcie pliku

**What:** Usunięcie testów `ConditionVariableHandler`.
**Why:** Testuje usuwany handler.
**Impact:** Brak.

#### Review & Status
- [ ok] Awaiting review

---

### Change 7: `test/handler/threaded_database_handler_test.cpp` — usunięcie pliku

**What:** Usunięcie testów `ThreadedDatabaseHandler`.
**Why:** Testuje usuwany handler.
**Impact:** Brak.

#### Review & Status
- [ ok] Awaiting review

---

### Change 8: `test/test.cpp` — usunięcie pliku

**What:** Usunięcie pliku. Zawiera `#include "server.hpp"`, dummy test `ASSERT_TRUE(true)` i zakomentowane legacy testy.
**Why:** Jedyny powód istnienia to legacy server. Dummy test nic nie wnosi.
**Impact:** Brak — test nic nie testuje.

#### Review & Status
- [ ok] Awaiting review

---

### Change 9: `src/main.cpp` — usunięcie `#include "server.hpp"`

**What:** Usunięcie linii `#include "server.hpp"` z main.cpp.
**Why:** Plik server.hpp jest usuwany.
**Impact:** Brak — main() jest pusty i nie używa żadnej klasy z server.hpp.

**Proposed code:**
```cpp
#include "single_thread_server.hpp"
#include "event.hpp"

int main()
{
}
```

#### Review & Status
- [ ok] Awaiting review

---

### Change 10: `CMakeLists.txt` — usunięcie source files z `myserver_lib`

**What:** Usunięcie z listy źródeł:
- `src/server/server.cpp`
- `src/handler/threaded_database_handler.cpp`
- `src/handler/condition_variable_database_handler.cpp`

Usunięcie z `target_include_directories`:
- `${CMAKE_SOURCE_DIR}/include/handler`

**Why:** Usuwane pliki źródłowe. Katalog handler nie będzie miał żadnych plików.
**Impact:** Niski — aktywny kod (logger, publisher, single_thread_server) nie zależy od tych plików.

**Proposed code (myserver_lib):**
```cmake
add_library(myserver_lib STATIC
    src/logger/log_names.cpp
)

target_include_directories(myserver_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/server
    ${CMAKE_SOURCE_DIR}/include/publisher
    ${CMAKE_SOURCE_DIR}/include/logger/registry
)
```

#### Review & Status
- [ ok] Awaiting review

---

### Change 11: `test/CMakeLists.txt` — usunięcie test files

**What:** Usunięcie z listy źródeł:
- `test.cpp`
- `handler/threaded_database_handler_test.cpp`
- `handler/condition_variable_handler_test.cpp`

**Why:** Usuwane pliki testowe.
**Impact:** Brak — pozostałe testy nie zależą od tych plików.

#### Review & Status
- [ ok] Awaiting review

---

## Build Errors (if any)

*(populated after build attempt)*

---

## Test Results (if any)

*(populated after test run)*
