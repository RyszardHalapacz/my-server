# ✅ CLOSED

# Proposal: Rozbicie server.h na osobne pliki

**Date:** 2026-03-30

---

## Original Prompt

Wszystko jest w serwer.h, weź to porozbijaj na odpowiednie pliki. Pamiętaj deklaracje w .h, definicje w .cpp. Przeszukaj repo i znajdź odpowiednie miejsca.

---

## Context

`server.h` zawiera ~800 linii: enumy, configy, hooki, command/result model, CRTP kontrakty,
Server class, Builder, SimpleMutexQueue. Trzeba rozbić na logiczne moduły.

Ograniczenie: Server, Builder, CRTP contracts i SimpleMutexQueue to szablony — MUSZĄ
zostać w headerach. Jedyny nie-szablonowy kod to hook noop functions → mogą pójść do .cpp.

Konwencja repo: `.hpp`, `snake_case`, headery w `include/`, źródła w `src/`, parowanie
`include/X/file.hpp` ↔ `src/X/file.cpp`.

---

## Proposed Changes

### Change 1: `include/server/server_types.hpp` — enumy

**What:** Nowy plik z enumami: ServerState, ShutdownMode, SubmitStatus, ProtocolKind, EndpointKind.
**Why:** Typy semantyczne używane przez wiele modułów — powinny być niezależne.
**Impact:** Zero — czysta ekstrakcja.

**Proposed code:**
```cpp
#pragma once

#include <cstdint>

namespace server {

enum class ServerState : uint8_t {
    created,
    running,
    stopping,
    stopped,
    failed
};

enum class ShutdownMode : uint8_t {
    graceful,
    force
};

enum class SubmitStatus : uint8_t {
    accepted,
    rejected_full,
    rejected_stopped,
    invalid,
    error
};

enum class ProtocolKind : uint8_t {
    mysql,
    tcp,
    http,
    custom
};

enum class EndpointKind : uint8_t {
    socket,
    file,
    terminal,
    custom
};

} // namespace server
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 2: `include/server/server_config.hpp` — structy konfiguracyjne

**What:** Nowy plik ze wszystkimi structami konfiguracyjnymi.
**Why:** Config jest cold-path, niezależny od reszty logiki serwera.
**Impact:** Zero — czysta ekstrakcja. Includuje server_types.hpp (potrzebuje ProtocolKind, EndpointKind).

**Proposed code:**
```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "server/server_types.hpp"

namespace server {

struct ListenerConfig {
    uint16_t port = 0;
    ProtocolKind protocol = ProtocolKind::custom;
    bool enabled = true;
};

struct ParserConfig {
    bool enable_sql_dialect = true;
    bool allow_partial_parse = false;
    uint32_t max_tokens = 0;
    uint32_t max_input_size = 0;
};

struct ExecutionConfig {
    uint32_t worker_count = 1;
    bool enable_backpressure = true;
    uint64_t queue_capacity = 1024;
};

struct PublisherConfig {
    bool enabled = true;
    bool publish_raw_event = false;
    bool publish_parsed_input = false;
    bool publish_command = false;
    bool publish_result = false;
};

struct DistributorConfig {
    bool enabled = true;
};

struct EndpointConfig {
    EndpointKind kind = EndpointKind::custom;
    bool enabled = true;
    std::string name{};
};

struct LoggerConfig {
    bool enabled = true;
    bool log_submit = false;
    bool log_parse = false;
    bool log_execute = false;
    bool log_publish = false;
    bool log_errors = true;
};

struct ServerConfig {
    std::vector<ListenerConfig> listeners{};
    std::vector<EndpointConfig> endpoints{};

    ParserConfig parser{};
    ExecutionConfig execution{};
    PublisherConfig publisher{};
    DistributorConfig distributor{};
    LoggerConfig logger{};
};

} // namespace server
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 3: `include/server/server_hooks.hpp` + `src/server/server_hooks.cpp` — hook table

**What:**
- Header: deklaracja ServerHookTable + deklaracje funkcji w hooks namespace
- Source: definicje noop funkcji + makeNoopHooks()

**Why:** Jedyny nie-szablonowy kod — separacja deklaracja/definicja zgodna z konwencją repo.
**Impact:** Zmiana z `inline` na deklaracja+definicja. Cold path — zero wpływu na performance.

**Proposed code (header):**
```cpp
#pragma once

#include <string_view>

#include "event.hpp"
#include "server/server_types.hpp"

namespace server {

struct ServerHookTable {
    using EventHook  = void(*)(const ::Event&) noexcept;
    using TextHook   = void(*)(std::string_view) noexcept;
    using StatusHook = void(*)(SubmitStatus) noexcept;

    EventHook  on_event_received = nullptr;
    StatusHook on_submit_result  = nullptr;

    TextHook on_raw_input        = nullptr;
    TextHook on_parsed_input     = nullptr;
    TextHook on_command_built    = nullptr;
    TextHook on_result_ready     = nullptr;
    TextHook on_distributed      = nullptr;
    TextHook on_error            = nullptr;
};

namespace hooks {

void noopEvent(const ::Event&) noexcept;
void noopText(std::string_view) noexcept;
void noopStatus(SubmitStatus) noexcept;

ServerHookTable makeNoopHooks() noexcept;

} // namespace hooks
} // namespace server
```

**Proposed code (source):**
```cpp
#include "server/server_hooks.hpp"

namespace server::hooks {

void noopEvent(const ::Event&) noexcept {}
void noopText(std::string_view) noexcept {}
void noopStatus(SubmitStatus) noexcept {}

ServerHookTable makeNoopHooks() noexcept {
    ServerHookTable h;
    h.on_event_received = &noopEvent;
    h.on_submit_result  = &noopStatus;
    h.on_raw_input      = &noopText;
    h.on_parsed_input   = &noopText;
    h.on_command_built  = &noopText;
    h.on_result_ready   = &noopText;
    h.on_distributed    = &noopText;
    h.on_error          = &noopText;
    return h;
}

} // namespace server::hooks
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 4: `include/server/command.hpp` — Command / Result model

**What:** Nowy plik z command::SelectCommand, command::InsertCommand, command::Command,
result::Row, result::Result.
**Why:** Domain model — niezależny od serwera, potencjalnie reużywalny.
**Impact:** Zero — czysta ekstrakcja.

**Proposed code:**
```cpp
#pragma once

#include <string>
#include <variant>
#include <vector>

namespace server::command {

struct SelectCommand {
    std::string table{};
    std::vector<std::string> columns{};
};

struct InsertCommand {
    std::string table{};
    std::vector<std::string> values{};
};

using Command = std::variant<SelectCommand, InsertCommand>;

} // namespace server::command

namespace server::result {

struct Row {
    std::vector<std::string> cells{};
};

struct Result {
    bool ok = false;
    std::string message{};
    std::vector<Row> rows{};
};

} // namespace server::result
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 5: `include/server/pipeline.hpp` — CRTP kontrakty + ListenerRuntime

**What:** Nowy plik z ProtocolAdapterCRTP, ParserCRTP, ExecutorCRTP, DistributorCRTP, ListenerRuntime.
**Why:** Kontrakty pipeline'u — logicznie spójny moduł, potencjalnie includeowany bez Server.
**Impact:** Szablony — muszą zostać w headerze. Zero wpływu.

**Proposed code:**
```cpp
#pragma once

#include <cstdint>
#include <string_view>

#include "event.hpp"
#include "server/server_types.hpp"
#include "server/command.hpp"

namespace server {

template <typename Derived>
class ProtocolAdapterCRTP {
public:
    [[nodiscard]] ProtocolKind kind() const noexcept {
        return d().kindImpl();
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

    [[nodiscard]] bool supportsPort(uint16_t port) const noexcept {
        return d().supportsPortImpl(port);
    }

    [[nodiscard]] bool decodeView(const ::Event& ev,
                                  std::string_view& out) noexcept {
        return d().decodeViewImpl(ev, out);
    }

protected:
    ~ProtocolAdapterCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

template <typename Derived>
class ParserCRTP {
public:
    [[nodiscard]] bool parse(std::string_view input,
                             command::Command& out) noexcept {
        return d().parseImpl(input, out);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

protected:
    ~ParserCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

template <typename Derived>
class ExecutorCRTP {
public:
    [[nodiscard]] bool execute(const command::Command& cmd,
                               result::Result& out) noexcept {
        return d().executeImpl(cmd, out);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

protected:
    ~ExecutorCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

template <typename Derived>
class DistributorCRTP {
public:
    void distribute(const result::Result& res) noexcept {
        d().distributeImpl(res);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return d().nameImpl();
    }

protected:
    ~DistributorCRTP() = default;

private:
    Derived& d() noexcept { return static_cast<Derived&>(*this); }
    const Derived& d() const noexcept { return static_cast<const Derived&>(*this); }
};

struct ListenerRuntime {
    uint16_t port = 0;
    ProtocolKind protocol = ProtocolKind::custom;
    bool enabled = true;
};

} // namespace server
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 6: `include/server/server.h` — okrojony do Server + Builder + SimpleMutexQueue

**What:** server.h includuje sub-headery i zawiera TYLKO szablony: Server, ServerBuilder, SimpleMutexQueue.
**Why:** Szablony MUSZĄ mieć definicje w headerze — nie da się ich przenieść do .cpp.
**Impact:** Publiczne API bez zmian. Plik z ~800 linii → ~350 linii.

**Proposed code:** server.h zamieniony na wersję includującą sub-headery + zawierającą szablony Server/Builder/SimpleMutexQueue (bez enumów, configów, hooków, command/result, CRTP).

#### Review & Status
- [ok ] Awaiting review

---

### Change 7: `CMakeLists.txt` — dodanie server_hooks.cpp do biblioteki

**What:** Dodanie `src/server/server_hooks.cpp` do `myserver_lib`.
**Why:** Nowy source file musi być skompilowany.

**Proposed code:**
```cmake
add_library(myserver_lib STATIC
    src/logger/log_names.cpp
    src/server/server_hooks.cpp
)
```

#### Review & Status
- [ok ] Awaiting review

---

## Build Errors (if any)

<populated after build attempt>

---

## Test Results (if any)

<populated after test run>
