# Proposal: Add `publisher_runtime.hpp`

**Date:** 2026-04-04

---

## Original Prompt

Brakujący plik includowany przez `publishable.hpp`. Wymagania:
- `Derived` nie zna sinków ani handle'i — dostarcza tylko `payload()`
- `PublisherRuntime` odpowiada za cały pipeline: resolve → channel → handle → zapis
- Hot path: `noexcept`, zero alokacji, zero virtual dispatch

---

## Context

`publishable.hpp` woła:
```cpp
publisher::runtime::PublisherRuntime<Sink>::publish(
    tokenRegistry, resourceStore, handle_.token(), self
);
```

`PublisherRuntime<Sink>` musi zrealizować pełny pipeline po stronie publishera.
`Derived` dostarcza tylko dane przez `payload()` — nic więcej.

---

## Proposed Changes

### Change 1: Nowy plik `include/publisher/runtime/publisher_runtime.hpp`

**What:** Trzy explicit specializations `PublisherRuntime<SinkKind>`.
Każda realizuje pipeline:
1. `registry.resolve(token)` → `OutputChannel`
2. `toIndex(channel)` → `idx`
3. pobranie uchwytu z `store[idx]`
4. `obj.payload()` → `std::string_view`
5. zapis do uchwytu

**Why:**
- Obiekt (`Derived`) jest całkowicie odseparowany od runtime — jedyny kontrakt to `payload()`.
- Publisher jest właścicielem całego routingu i zapisu.
- Primary template niezdefiniowany → nieznany `SinkKind` = błąd kompilacji.
- `noexcept` + `assert()` na null handle — zero kosztu w release.
- `SocketHandle` jest aktualnie stubem → `(void)obj` do czasu implementacji.

**Kontrakt `Derived`:**
```cpp
std::string_view payload() const noexcept;
```

**Proposed code:**
```cpp
//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHER_RUNTIME_HPP
#define MYSERVER_PUBLISHER_RUNTIME_HPP

#include <cassert>
#include <string_view>

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/resource_store.hpp"
#include "publisher/runtime/token_registry.hpp"

namespace publisher::runtime
{
    // Primary template — intentionally undefined.
    // Using an unregistered SinkKind is a compile-time error.
    template<publisher::core::SinkKind Sink>
    struct PublisherRuntime;

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::Terminal>
    {
        template<typename Derived>
        static void publish(TokenRegistry& registry,
                            OutputResourceStore& store,
                            publisher::core::PublishToken token,
                            const Derived& obj) noexcept
        {
            const auto  channel = registry.resolve(token);
            const auto  idx     = publisher::core::toIndex(channel);
            const auto& handle  = store.terminals[idx];

            assert(handle.out != nullptr && "TerminalHandle: null stream");

            const std::string_view data = obj.payload();
            handle.out->write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    };

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::File>
    {
        template<typename Derived>
        static void publish(TokenRegistry& registry,
                            OutputResourceStore& store,
                            publisher::core::PublishToken token,
                            const Derived& obj) noexcept
        {
            const auto  channel = registry.resolve(token);
            const auto  idx     = publisher::core::toIndex(channel);
            const auto& handle  = store.files[idx];

            assert(handle.file != nullptr && "FileHandle: null stream");

            const std::string_view data = obj.payload();
            handle.file->write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    };

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::Socket>
    {
        template<typename Derived>
        static void publish(TokenRegistry& registry,
                            OutputResourceStore& store,
                            publisher::core::PublishToken token,
                            const Derived& obj) noexcept
        {
            const auto  channel = registry.resolve(token);
            const auto  idx     = publisher::core::toIndex(channel);
            const auto& handle  = store.sockets[idx];

            assert(handle.fakeFd >= 0 && "SocketHandle: invalid fd");

            // TODO: real socket write — fakeFd is a placeholder
            (void)obj;
            (void)handle;
        }
    };
} // namespace publisher::runtime

#endif // MYSERVER_PUBLISHER_RUNTIME_HPP
```

#### Review & Status
- [ ] **WSTRZYMANY** — kierunek poprawny, ale kontrakt payloadu (`std::string_view payload()`) zbyt konkretny na tym etapie. Uzgodnione zasady:
  - `Derived` nie zna sinków ani handle'i
  - `Derived` dostarcza dane / payload
  - `PublisherRuntime` wykonuje publikację
  - Forma payloadu do ustalenia osobno

---

## Build Errors (if any)

_Populated after build._

---

## Test Results (if any)

_Populated after tests._
