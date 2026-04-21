# Proposal: Split into self-contained modules (common / publisher / logger)

**Date:** 2026-04-07

---

## Original Prompt

> stary porozkminaj moje repozytorium pomysl and dwaoa rzeczami, zrobic odzielne libki dla publishera i loggera, ktore bysmy dolaczali do kodu produkcyjnego, w ramach tego repo? [...] w ramach tej binarki mielibysmy testy wewnatz naszych subrepo publisher i logger
>
> [po wyjaśnieniach] to zrob ta dobra wersje
>
> dawaj trudnijsza wersje

**"Trudniejsza wersja"** (moja interpretacja — potwierdź lub skoryguj):

Każdy moduł (`common`, `publisher`, `logger`) jest **samodzielnym subprojektem CMake**:
- własny folder `modules/<name>/` zawierający `include/`, `src/`, `test/`, `CMakeLists.txt`
- można go zbudować **standalone**: `cmake -S modules/logger -B build-logger` (bez root projektu)
- ma swoje **własne testy wewnątrz** (`modules/<name>/test/` — dokładnie jak napisałeś: *"testy wewnątrz subrepo publisher i logger"*)
- eksportuje się przez `install(TARGETS ... EXPORT ...)` — zewnętrzny konsument może `find_package(Publisher CONFIG)` i użyć
- root `CMakeLists.txt` po prostu `add_subdirectory(modules/common)` itd., spaja wszystko dla `MyApp` i integration/stress testów które potrzebują wielu modułów

Jeśli miałeś na myśli coś innego (np. git submodules jako fizycznie osobne repo, albo conan packaging) — powiedz.

---

## Context

### Obecny stan

- Jeden target `myserver_lib` miesza `logger`/`publisher`/`server`.
- Wszystkie nagłówki siedzą flat w `include/<module>/`, źródła w `src/<module>/`.
- Wszystkie testy w jednym `main_test`.

### Blokada architektoniczna — cykl

```
logger/core/log_engine.hpp    ──▶  publisher/runtime/*.hpp     (OK: LogEngine jest klientem publishera)
publisher/sink_publisher.hpp  ──▶  logger/log_names.hpp        (PRZECIEK)
```

`log_names.hpp` nie zawiera nic logger-specific — to cienkie mapowanie enumów z `common/log_ids.def` → `string_view`. Jest w `logger/` historycznie. **Przeniesienie do `common/` eliminuje cykl czysto.**

### Docelowa struktura katalogów

```
my-server/
├── CMakeLists.txt                        ← root orchestrator
├── modules/
│   ├── common/
│   │   ├── CMakeLists.txt                ← standalone-buildable
│   │   ├── include/common/
│   │   │   ├── log_ids.hpp
│   │   │   ├── log_ids.def
│   │   │   ├── log_classes.def
│   │   │   ├── log_names.hpp             ← przeniesione z logger/
│   │   │   └── messages/...              ← już tam są
│   │   ├── src/
│   │   │   └── log_names.cpp             ← przeniesione z src/logger/
│   │   └── test/
│   │       ├── CMakeLists.txt
│   │       └── log_names_test.cpp        ← przeniesione z test/logger/
│   │
│   ├── publisher/
│   │   ├── CMakeLists.txt
│   │   ├── include/publisher/
│   │   │   ├── core/...
│   │   │   ├── runtime/...
│   │   │   ├── publisher.hpp
│   │   │   ├── sink_publisher.hpp
│   │   │   └── policy_publisher.hpp
│   │   ├── src/
│   │   │   └── sink_publisher.cpp
│   │   └── test/
│   │       ├── CMakeLists.txt
│   │       ├── publisher_test.cpp
│   │       ├── policy/policy_test.cpp
│   │       ├── sink/json_sink_test.cpp
│   │       ├── sink/text_sink_test.cpp
│   │       └── runtime/publisher_runtime_test.cpp
│   │
│   └── logger/
│       ├── CMakeLists.txt
│       ├── include/logger/
│       │   ├── core/...
│       │   ├── registry/...
│       │   └── logger.hpp
│       ├── src/
│       │   └── core/log_engine.cpp
│       └── test/
│           ├── CMakeLists.txt
│           ├── header_args_test.cpp
│           ├── logger_header_smoke_test.cpp
│           ├── logger_header_negative_test.cpp
│           ├── payloads/...
│           ├── core/
│           │   ├── stream_adapter_test.cpp
│           │   ├── log_record_test.cpp
│           │   ├── freelist_test.cpp
│           │   └── mpsc_queue_test.cpp
│           └── mocks/mock_log_engine.hpp
│
├── app/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp
│       └── server/server_hooks.cpp
│
├── test/                                 ← cross-module tylko
│   ├── CMakeLists.txt
│   ├── integration/
│   │   ├── log_engine_pipeline_test.cpp  ← z logger/core/
│   │   └── full_pipeline_test.cpp        ← z logger/integration/
│   ├── stress/
│   │   ├── mpsc_queue_stress_test.cpp
│   │   ├── freelist_stress_test.cpp
│   │   ├── log_engine_stress_test.cpp
│   │   └── harness/stress_builder.hpp
│   └── server/
│       ├── metrics_mixin_test.cpp
│       └── server_components_test.cpp
```

### Dlaczego każdy moduł standalone-buildable

Realny benefit:
1. **Wymusza poprawność zależności.** Jeśli ktoś przypadkiem doda `#include "logger/..."` w publisherze, standalone build publishera zawali się — nie ma tam loggera. Dziś to by się "ukryło" bo wszystko jest w jednym drzewie.
2. **Szybki cykl pracy nad jednym modułem:** `cmake --build build-publisher && ctest --test-dir build-publisher` — nie buduje nic innego.
3. **Gotowość do wyjścia na zewnątrz.** `install(EXPORT)` + `find_package(Publisher CONFIG)` = moduł można skopiować do innego repo albo opublikować jako pakiet.
4. **Dzieli duże refactory na audytowalne kroki** — każdy moduł testuje się osobno.

Koszt: duplikacja boilerplate CMake + tooling (`CMakePackageConfigHelpers`), ale to jednorazowe.

### Założenia

- `modules/logger` linkuje `publisher` **PUBLIC** (bo `log_engine.hpp` inkluduje nagłówki publishera w interfejsie).
- `modules/publisher` linkuje `common` **PUBLIC** (bo `sink_publisher.hpp` inkluduje `common/log_names.hpp`).
- Używamy `BUILD_TESTING` (standardowy CMake flag) + per-module override `<MODULE>_BUILD_TESTS` aby testy modułu dało się zbudować niezależnie.
- `find_package(GTest)` jako fallback dla standalone build, `FetchContent` dla głównego builda (pozostaje w `modules/common` — logicznie common zapewnia test infra, ale otwarty na dyskusję).
- Modułów **nie przenosimy do osobnych git repo / submodules** — to "subrepo" w sensie strukturalnym, nie VCS-owym (chyba że powiesz inaczej).

---

## Proposed Changes

### Change 1: Przeciąć cykl — przenieść `log_names` do `common/`

**What:**
- `include/logger/log_names.hpp` → `modules/common/include/common/log_names.hpp`
- `src/logger/log_names.cpp` → `modules/common/src/log_names.cpp`
- Update `#include` we wszystkich konsumentach:
  - `include/publisher/sink_publisher.hpp`: `"logger/log_names.hpp"` → `"common/log_names.hpp"`
  - `src/common/log_names.cpp` (self-include): `"logger/log_names.hpp"` → `"common/log_names.hpp"`
  - `test/logger/log_names_test.cpp` → przeniesienie do `modules/common/test/log_names_test.cpp` + update include

**Why:**
Zawartość `log_names.hpp` to *wyłącznie* mapowanie enumów z `common/log_ids.def`. Nic w nim nie jest logger-specific. Obecna lokalizacja w `logger/` to historyczny artefakt i jedyna przyczyna cyklu `publisher → logger`. Bez tej zmiany żaden split targetów się nie uda — linker nie rozwiąże cyklu bez brzydkich trików (`--start-group`).

**Impact:**
- Zero runtime — wszystko pozostaje `constexpr` / inline const arrays.
- Eliminuje cykl, co jest prerequisite'em dla wszystkich kolejnych Changes.
- Ryzyko niskie: mechaniczna zmiana include path. API pozostaje identyczne (funkcje `className()`, `methodName()`, `methodClass()` w global namespace bez zmian).

**Proposed code** — nowy `modules/common/include/common/log_names.hpp`:
```cpp
#pragma once
#include <string_view>
#include "common/log_ids.hpp"

std::string_view className(LogClassId);
std::string_view methodName(MethodId);
LogClassId       methodClass(MethodId);
```

Nowy `modules/common/src/log_names.cpp` (treść identyczna, tylko self-include):
```cpp
#include <array>
#include <string_view>

#include "common/log_ids.hpp"
#include "common/log_names.hpp"

// kClassNames, kMethodNames, kMethodClass — bez zmian
// definicje className/methodName/methodClass — bez zmian
```

Update `include/publisher/sink_publisher.hpp` (będzie przeniesiony do `modules/publisher/` w Change 3):
```cpp
#include "common/log_names.hpp"   // było: "logger/log_names.hpp"
#include "common/log_ids.hpp"
```

#### Review & Status
- [x] Approved and applied

---

### Change 2: Stworzyć `modules/common/` jako standalone CMake subproject

**What:**
- Przenieść wszystkie pliki z `include/common/` → `modules/common/include/common/`
  - `log_ids.hpp`, `log_ids.def`, `log_classes.def`, `messages/*`
- Przenieść `log_names.hpp` / `log_names.cpp` (z Change 1) do `modules/common/`.
- Stworzyć `modules/common/CMakeLists.txt` — standalone-buildable.
- Dodać `install(TARGETS common EXPORT ...)`.

**Why:**
Fundament dla pozostałych modułów. `publisher` i `logger` zależą od `common`. Musi być pierwszy, bo bez niego nic się nie linkuje.

**Impact:**
- Standalone build: `cmake -S modules/common -B build-common && cmake --build build-common`.
- Testy: `modules/common/test/log_names_test.cpp` odpala się przez `ctest --test-dir build-common`.
- Publiczny header directory: `modules/common/include`, który zawiera katalog `common/` — czyli konsumenci pisza `#include "common/log_names.hpp"`. Identyczna konwencja jak dziś.

**Proposed code** — `modules/common/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(common CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COMMON_BUILD_TESTS "Build common tests" ${PROJECT_IS_TOP_LEVEL})

add_library(common STATIC
    src/log_names.cpp
)
add_library(common::common ALIAS common)

target_include_directories(common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(common PUBLIC cxx_std_20)
target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic)

# ── install / export ──────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS common
    EXPORT commonTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT commonTargets
    FILE commonTargets.cmake
    NAMESPACE common::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common
)

# ── tests ─────────────────────────────────────────────────────────────────────
if(COMMON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

`modules/common/test/CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(common_tests
    log_names_test.cpp
)
target_link_libraries(common_tests PRIVATE common::common GTest::gtest_main)
target_compile_options(common_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(common_tests)
```

#### Review & Status
- [x] Approved and applied

---

### Change 3: Stworzyć `modules/publisher/` jako standalone CMake subproject

**What:**
- Przenieść `include/publisher/**` → `modules/publisher/include/publisher/**`
- Przenieść `src/publisher/sink_publisher.cpp` → `modules/publisher/src/sink_publisher.cpp`
- Przenieść testy publishera z `test/publisher/**` → `modules/publisher/test/**`:
  - `publisher_test.cpp`
  - `policy/policy_test.cpp`
  - `sink/json_sink_test.cpp`
  - `sink/text_sink_test.cpp`
  - `runtime/publisher_runtime_test.cpp`
- Stworzyć `modules/publisher/CMakeLists.txt` — standalone-buildable.
- Moduł zależy **tylko od `common`** — tym właśnie wymuszamy brak przecieku do loggera.

**Why:**
Izoluje publisher jako samodzielny moduł. Standalone build tego modułu jest twardym testem, że publisher nie widzi loggera — bo go tam fizycznie nie ma.

**Impact:**
- Publisher dalej ma `PUBLIC` include `modules/publisher/include` — konsumenci piszą `#include "publisher/runtime/publisher_runtime.hpp"`, identycznie jak dziś.
- `target_link_libraries(publisher PUBLIC common::common)` — bo `sink_publisher.hpp` inkluduje `common/log_names.hpp` w headerze (publiczny interfejs).
- `find_package(common CONFIG REQUIRED)` w trybie standalone, `if(NOT TARGET common::common)` fallback jeśli top-level build już dodał target.

**Proposed code** — `modules/publisher/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(publisher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PUBLISHER_BUILD_TESTS "Build publisher tests" ${PROJECT_IS_TOP_LEVEL})

# Find or re-use common
if(NOT TARGET common::common)
    find_package(common CONFIG REQUIRED)
endif()

add_library(publisher STATIC
    src/sink_publisher.cpp
)
add_library(publisher::publisher ALIAS publisher)

target_include_directories(publisher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(publisher PUBLIC common::common)
target_compile_features(publisher PUBLIC cxx_std_20)
target_compile_options(publisher PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS publisher
    EXPORT publisherTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT publisherTargets
    FILE publisherTargets.cmake
    NAMESPACE publisher::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/publisher
)

if(PUBLISHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

`modules/publisher/test/CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(publisher_tests
    publisher_test.cpp
    policy/policy_test.cpp
    sink/json_sink_test.cpp
    sink/text_sink_test.cpp
    runtime/publisher_runtime_test.cpp
)
target_include_directories(publisher_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(publisher_tests PRIVATE publisher::publisher GTest::gtest_main)
target_compile_options(publisher_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(publisher_tests)
```

**Open question:** GTest `FetchContent` jest w KAŻDYM `test/CMakeLists.txt` — to rebuildy gtest'a w trybie standalone. Alternatywa: wrzucić fetch do top-level i w submodułach użyć `find_package(GTest)` jako fallback. **Proponuję:** top-level deduplikacja w Change 6, submoduły mają `if(NOT TARGET GTest::gtest_main) <FetchContent> endif()`. Do dogadania.

#### Review & Status
- [x] Approved and applied

---

### Change 4: Stworzyć `modules/logger/` jako standalone CMake subproject

**What:**
- Przenieść `include/logger/**` (**z wyłączeniem** `log_names.hpp` — już w common) → `modules/logger/include/logger/**`
- Przenieść `src/logger/core/log_engine.cpp` → `modules/logger/src/core/log_engine.cpp`
  - `src/logger/telemetry/` jest pusty — ignorujemy
  - `src/logger/log_names.cpp` już przeniesione (Change 1)
- Przenieść testy loggera z `test/logger/**` → `modules/logger/test/**`:
  - `header_args_test.cpp`, `logger_header_smoke_test.cpp`, `logger_header_negative_test.cpp`
  - `payloads/*`
  - `core/stream_adapter_test.cpp`, `core/log_record_test.cpp`, `core/freelist_test.cpp`, `core/mpsc_queue_test.cpp`
  - `mocks/mock_log_engine.hpp`
  - **NIE** przenosimy: `log_names_test.cpp` (poszedł do common w Change 1), `core/log_engine_pipeline_test.cpp` (integration — Change 6), `integration/full_pipeline_test.cpp` (integration — Change 6)
- Stworzyć `modules/logger/CMakeLists.txt`.
- Zależy od `common` i `publisher` (PUBLIC — interfejs loggera używa obu).

**Why:**
Logger to finalny moduł w łańcuchu. Po tej zmianie każdy moduł jest samodzielny i ma własny test suite.

**Impact:**
- Standalone build loggera wymaga dostępnego `common` i `publisher` — albo top-level build, albo `find_package` po zainstalowaniu tych dwóch.
- `log_engine.hpp` publicznie inkluduje nagłówki publishera → `target_link_libraries(logger PUBLIC publisher::publisher)`.
- `mpsc_queue_stress_test.cpp` / `freelist_stress_test.cpp` / `log_engine_stress_test.cpp` — **stress testy NIE lądują w module logger**, bo są w dziale cross-module (test/stress/) i mogą potrzebować innego setup (długie czasy, benchmark, specific sanitizers). Zostają w root `test/stress/`.

**Proposed code** — `modules/logger/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(logger CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOGGER_BUILD_TESTS "Build logger tests" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET common::common)
    find_package(common CONFIG REQUIRED)
endif()
if(NOT TARGET publisher::publisher)
    find_package(publisher CONFIG REQUIRED)
endif()

add_library(logger STATIC
    src/core/log_engine.cpp
)
add_library(logger::logger ALIAS logger)

target_include_directories(logger
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(logger
    PUBLIC
        common::common
        publisher::publisher
)
target_compile_features(logger PUBLIC cxx_std_20)
target_compile_options(logger PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS logger
    EXPORT loggerTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT loggerTargets
    FILE loggerTargets.cmake
    NAMESPACE logger::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logger
)

if(LOGGER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
```

`modules/logger/test/CMakeLists.txt`:
```cmake
include(FetchContent)
if(NOT TARGET GTest::gtest_main)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

add_executable(logger_tests
    header_args_test.cpp
    logger_header_smoke_test.cpp
    logger_header_negative_test.cpp
    payloads/payload_base_test.cpp
    payloads/request_payload_test.cpp
    payloads/payload_register_test.cpp
    payloads/builder_test.cpp
    core/stream_adapter_test.cpp
    core/log_record_test.cpp
    core/freelist_test.cpp
    core/mpsc_queue_test.cpp
)
target_include_directories(logger_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(logger_tests PRIVATE logger::logger GTest::gtest_main)
target_compile_options(logger_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(logger_tests)
```

#### Review & Status
- [x] Approved and applied

---

### Change 5: Stworzyć `app/` dla `MyApp`

**What:**
- Przenieść `src/main.cpp` → `app/src/main.cpp`
- Przenieść `src/server/server_hooks.cpp` → `app/src/server/server_hooks.cpp`
- Przenieść `include/server/**`, `include/event.hpp`, `include/handler/**`, `include/common.h`, `lib/httplib.h` — te rzeczy używa tylko aplikacja, więc idą do `app/` (`app/include/`, `app/lib/`) — **nie do modułów**.
- Stworzyć `app/CMakeLists.txt` linkujący `logger`, `publisher`, `common`.

**Why:**
`MyApp` jest **konsumentem** modułów, nie jednym z nich. Wyciągnięcie go do `app/` czyści korzeń repo — w roocie zostają tylko `modules/`, `app/`, `test/`, top-level `CMakeLists.txt`. `server_hooks.cpp` ma tylko jednego konsumenta (`main.cpp`), więc nie robimy z niego osobnej libki — byłaby premature abstraction.

**Impact:**
- Ścieżki include w `main.cpp` — zmienić `#include "server/server.h"` itp. na ścieżki relative do `app/include/` (lub dodać `target_include_directories(MyApp PRIVATE app/include)`).
- Ryzyko: są nagłówki które dziś konsumenci widzą przez `myserver_lib` (np. `include/handler/`, `include/common.h`). Potrzebuję szybkiego `grep`a żeby upewnić się co dokładnie jest tylko w `app`. **Do zrobienia przed apply.**

**Proposed code** — `app/CMakeLists.txt`:
```cmake
add_executable(MyApp
    src/main.cpp
    src/server/server_hooks.cpp
)

target_include_directories(MyApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(MyApp PRIVATE
    logger::logger
    publisher::publisher
    common::common
)

target_compile_options(MyApp PRIVATE -Wall -Wextra -Wpedantic)
```

#### Review & Status
- [x] Approved and applied

---

### Change 6: Root `CMakeLists.txt` + cross-module `test/`

**What:**
Nowy top-level `CMakeLists.txt` spaja wszystko:
- `add_subdirectory(modules/common)` / `publisher` / `logger`
- `add_subdirectory(app)`
- `add_subdirectory(test)` — cross-module testy (integration / stress / server)
- Centralna konfiguracja GTest przez `FetchContent` — submoduły używają już gotowego `GTest::gtest_main`.

`test/CMakeLists.txt` (root) — zawiera tylko testy które potrzebują **wielu modułów**:
- `integration_tests`: `log_engine_pipeline_test.cpp`, `full_pipeline_test.cpp`
- `stress_tests`: `mpsc_queue_stress_test.cpp`, `freelist_stress_test.cpp`, `log_engine_stress_test.cpp` + `harness/stress_builder.hpp`
- `server_tests`: `metrics_mixin_test.cpp`, `server_components_test.cpp`

**Why:**
- Top-level build działa tak, jak lokalny programista tego oczekuje: `cmake -B build && cmake --build build && ctest --test-dir build` i wszystko się buduje i testy odpalają.
- Cross-module testy są wyraźnie oznakowane — jeśli integration test wymaga loggera + publishera razem, to **z definicji** nie należy do żadnego modułu.
- GTest fetch w top-level = tylko jeden fetch dla całego builda. W standalone build każdy moduł robi własny fetch (z guardem `if(NOT TARGET GTest::gtest_main)`) — akceptowalne.

**Impact:**
- `server_tests` — przeglądnę zawartość tych testów przed apply (nie wiem jeszcze, od czego zależą — może tylko od `common`? wtedy można je zostawić w `modules/common/test/`, ale na razie zakładam że cross-module).
- Stress testy: zachowujemy fizyczną lokalizację w root `test/stress/` — nie w `modules/logger/test/`. Powód: stress mają inny charakter (długie runy, inne flagi), są uruchamiane osobno w CI.
- `harness/stress_builder.hpp` zostaje z stress testami.

**Proposed code** — nowy root `CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)
project(MyServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MYSERVER_BUILD_TESTS "Build all tests" ON)

# ── central GTest (reused by all modules & cross-module tests) ───────────────
if(MYSERVER_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ── per-module test toggles (follow top-level) ───────────────────────────────
set(COMMON_BUILD_TESTS    ${MYSERVER_BUILD_TESTS} CACHE BOOL "" FORCE)
set(PUBLISHER_BUILD_TESTS ${MYSERVER_BUILD_TESTS} CACHE BOOL "" FORCE)
set(LOGGER_BUILD_TESTS    ${MYSERVER_BUILD_TESTS} CACHE BOOL "" FORCE)

# ── modules ──────────────────────────────────────────────────────────────────
add_subdirectory(modules/common)
add_subdirectory(modules/publisher)
add_subdirectory(modules/logger)

# ── application ──────────────────────────────────────────────────────────────
add_subdirectory(app)

# ── cross-module tests ───────────────────────────────────────────────────────
if(MYSERVER_BUILD_TESTS)
    add_subdirectory(test)
endif()
```

Nowy root `test/CMakeLists.txt` (tylko cross-module):
```cmake
include(GoogleTest)

# ── integration_tests ─────────────────────────────────────────────────────────
add_executable(integration_tests
    integration/log_engine_pipeline_test.cpp
    integration/full_pipeline_test.cpp
)
target_include_directories(integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(integration_tests
    PRIVATE logger::logger publisher::publisher common::common GTest::gtest_main)
target_compile_options(integration_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(integration_tests)

# ── stress_tests ──────────────────────────────────────────────────────────────
add_executable(stress_tests
    stress/mpsc_queue_stress_test.cpp
    stress/freelist_stress_test.cpp
    stress/log_engine_stress_test.cpp
)
target_include_directories(stress_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stress  # dla harness/
)
target_link_libraries(stress_tests
    PRIVATE logger::logger publisher::publisher common::common GTest::gtest_main)
target_compile_options(stress_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(stress_tests)

# ── server_tests ──────────────────────────────────────────────────────────────
add_executable(server_tests
    server/metrics_mixin_test.cpp
    server/server_components_test.cpp
)
target_include_directories(server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app/include   # bo server_tests może potrzebować nagłówków z app/
)
target_link_libraries(server_tests
    PRIVATE logger::logger publisher::publisher common::common GTest::gtest_main)
target_compile_options(server_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(server_tests)
```

#### Review & Status
- [x] Approved and applied

---

### Change 7: Dodać `<name>Config.cmake.in` dla każdego modułu (install-side)

**What:**
Dla każdego modułu dodać plik `modules/<name>/cmake/<name>Config.cmake.in`, który CMake użyje do wygenerowania `<Name>Config.cmake` przy `install(EXPORT)`. Bez tego `find_package(common CONFIG)` nie zadziała od strony konsumenta.

**Why:**
Obecny `install(EXPORT ...)` generuje tylko plik z targetami, ale konsument musi widzieć `commonConfig.cmake` żeby `find_package(common CONFIG REQUIRED)` działał. Change 7 uzupełnia infrastrukturę pakietowania — bez niego Changes 2-4 działają tylko wewnątrz tego samego builda, a nie zewnętrznie.

**Impact:**
- Tylko pliki konfiguracyjne, zero zmian w runtime.
- Umożliwia prawdziwy external consumption.

**Proposed code** — `modules/common/cmake/commonConfig.cmake.in`:
```cmake
@PACKAGE_INIT@

include("${CMAKE_CURRENT_LIST_DIR}/commonTargets.cmake")
check_required_components(common)
```

Dodać na końcu `modules/common/CMakeLists.txt`:
```cmake
configure_package_config_file(
    cmake/commonConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/commonConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/commonConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common
)
```

Analogicznie dla `publisher` i `logger`. `publisherConfig.cmake.in` musi dodatkowo zawierać:
```cmake
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(common CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/publisherTargets.cmake")
check_required_components(publisher)
```

`loggerConfig.cmake.in`:
```cmake
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(common CONFIG)
find_dependency(publisher CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/loggerTargets.cmake")
check_required_components(logger)
```

#### Review & Status
- [x] Approved and applied

---

## Open Questions (do potwierdzenia przed apply)

1. **Struktura folderów: `modules/` czy inny prefix?** Widziałem w różnych projektach `libs/`, `packages/`, `components/`. `modules/` jest neutralne — akceptujesz?
2. **Git submodules?** Czy zostajemy przy in-tree `modules/` (mój default), czy chcesz żeby każdy moduł był **osobnym git repo** dołączanym jako submodule? To znaczący koszt zarządzania — odradzam dla projektu jednoosobowego, ale to Twoja decyzja.
3. **Change 7 (install/export):** Potrzebne Ci to TERAZ, czy wystarczy top-level build i Change 7 możemy odłożyć jako opcjonalny?
4. **`server_tests`**: Muszę zerknąć co dokładnie testują. Jeśli nie dotykają loggera/publishera, to można je przenieść do `app/test/` jako app-level, zamiast trzymać w root `test/`. Zostawiam w root jako safe default — do korekty po przejrzeniu.
5. **Stress testy**: zostają w root `test/stress/` czy przenieść do `modules/logger/test/stress/`? Mój głos: zostają w root, bo stress != unit i często są uruchamiane osobno.
6. **Kolejność apply:** Changes 1 → 2 → 3 → 4 → 5 → 6 → 7 to logiczny porządek. **Nie możemy** wcześniej zbudować bez wszystkich tych zmian — intermediate states się nie zbudują. Proponuję podejście: po approve wszystkich Changes, apply wszystkich na raz, potem build. **Jesteś na to gotowy?**

---

## Build Errors (if any)

### Attempt 1 — 2026-04-07

Two real errors (oprócz paru `-Wunused-result`/`-Wunused-variable` w `publisher_runtime_test.cpp`, które są *pre-existing*, nie dotykamy):

**Error A — `integration_tests` / `full_pipeline_test.cpp`**
```
full_pipeline_test.cpp:2:10: fatal error: ../test/logger/mocks/mock_log_engine.hpp: No such file or directory
    2 | #include "../test/logger/mocks/mock_log_engine.hpp"
```
Przyczyna: stary, relatywny include. Mock przeniesiony do `modules/logger/test/mocks/mock_log_engine.hpp` podczas refaktoru.

**Error B — `server_tests` linking**
```
server_components_test.cpp: undefined reference to
  server::hooks::makeNoopHooks()
  server::hooks::noopText(std::string_view)
  server::hooks::noopEvent(Event const&)
  server::hooks::noopStatus(server::SubmitStatus)
```
Przyczyna: `server_components_test.cpp` używa symboli z `server_hooks.cpp`, który teraz jest kompilowany tylko do `MyApp`. `server_tests` go nie linkuje.

---

### Proposed Fixes

#### Fix A1: `test/integration/full_pipeline_test.cpp` — zmiana ścieżki include

**What:** Zamienić relatywny include na logiczną ścieżkę od katalogu testu modułu logger.

**Why:** Relatywne `../test/logger/...` nie ma sensu po refaktorze. Mock jest teraz w `modules/logger/test/mocks/` — ponieważ jest to **helper testowy innego modułu**, najczystsze rozwiązanie to dodać jego katalog do include dirs `integration_tests` i inkludować przez `"mocks/mock_log_engine.hpp"`.

**Impact:** Zero runtime, tylko build-time. Integration test jest z definicji cross-module, więc sięganie do test helpers innego modułu jest OK.

**Proposed code:**

`test/integration/full_pipeline_test.cpp` linia 2:
```cpp
#include "mocks/mock_log_engine.hpp"   // było: "../test/logger/mocks/mock_log_engine.hpp"
```

`test/CMakeLists.txt` — dodać include dir do `integration_tests`:
```cmake
target_include_directories(integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration
    ${CMAKE_SOURCE_DIR}/modules/logger/test     # ← NEW: dostęp do mocks/*
)
```

#### Review & Status
- [x] Approved and applied

---

#### Fix B1: `app/test/CMakeLists.txt` — dodać `server_hooks.cpp` do `server_tests`

**What:** Skompilować `server_hooks.cpp` jako dodatkowe źródło `server_tests`, analogicznie jak jest w `MyApp`.

**Why:** `server_tests` testują m.in. `server::hooks::makeNoopHooks`/`noopText`/`noopEvent`/`noopStatus` — definicje tych funkcji żyją w `app/src/server_hooks.cpp`. Bez linkowania tego pliku dostajemy undefined references.

**Alternatywa (odrzucona):** wyciągnąć `server_hooks.cpp` do osobnego `OBJECT`/`STATIC` targetu `app_server_obj` i linkować oba executables. Czyściej architektonicznie, ale to 1 plik — nie uzasadnia dodatkowego targetu. Prostszy fix: ten sam plik kompilowany w 2 miejscach (cold path, build-time only, bez impact na runtime).

**Impact:** Wyłącznie build-time. Jeden plik kompilowany 2x.

**Proposed code:**

`app/test/CMakeLists.txt`:
```cmake
include(GoogleTest)

# ── server_tests (app-level — not cross-module) ──────────────────────────────
add_executable(server_tests
    server/metrics_mixin_test.cpp
    server/server_components_test.cpp
    ${CMAKE_SOURCE_DIR}/app/src/server_hooks.cpp   # ← NEW
)
target_include_directories(server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app/include
)
target_link_libraries(server_tests PRIVATE GTest::gtest_main)
target_compile_options(server_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(server_tests)
```

#### Review & Status
- [x] Approved and applied

---

## Test Results (if any)

*(populated after test run)*
