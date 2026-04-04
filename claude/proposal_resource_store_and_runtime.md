# ✅ CLOSED (Change 2 przeniesiony do proposal_publisher_runtime.md — wstrzymany)
# Proposal: Add `resource_store.hpp` and `publisher_runtime.hpp`

**Date:** 2026-04-04

---

## Original Prompt

"tak" — prośba o przygotowanie propozycji dla dwóch brakujących plików includowanych przez `publishable.hpp`:
- `publisher/runtime/resource_store.hpp`
- `publisher/runtime/publisher_runtime.hpp`

---

## Context

`publishable.hpp` kompiluje się tylko jeśli te dwa pliki istnieją. Ich rol wynika z użycia w `Publishable::publish()`:

```cpp
publisher::runtime::PublisherRuntime<Sink>::publish(
    tokenRegistry, resourceStore, handle_.token(), self
);
```

`OutputResourceStore` musi trzymać uchwyty do sinków (Terminal/File/Socket) dla każdego kanału.
`PublisherRuntime<Sink>` musi rozwiązać token → kanał → uchwyt → wywołać `emit` na Derived (CRTP).

Oba pliki muszą być zgodne z CLAUDE.md:
- zero heap
- brak virtual dispatch
- `noexcept` na hot path
- statyczny polimorfizm (explicit template specializations)

---

## Proposed Changes

### Change 1: Nowy plik `include/publisher/runtime/resource_store.hpp`

**What:** Agregat `OutputResourceStore` — trzy płaskie tablice uchwytów (po jednej na `SinkKind`), indeksowane przez `OutputChannel`.

**Why:** `PublisherRuntime` musi mieć skąd pobrać uchwyt dla danego kanału. Płaskie `std::array` = zero overhead, zero alokacji, przewidywalny layout pamięci.

**Impact:** Brak wpływu na istniejący kod. Tylko nowy typ.

**Proposed code:**
```cpp
//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_RESOURCE_STORE_HPP
#define MYSERVER_RESOURCE_STORE_HPP

#include <array>
#include <cstddef>

#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/sink_handles.hpp"

namespace publisher::runtime
{
    struct OutputResourceStore
    {
        static constexpr std::size_t kChannelCount =
            static_cast<std::size_t>(publisher::core::OutputChannel::Count);

        std::array<TerminalHandle, kChannelCount> terminals{};
        std::array<FileHandle,     kChannelCount> files{};
        std::array<SocketHandle,   kChannelCount> sockets{};
    };
} // namespace publisher::runtime

#endif // MYSERVER_RESOURCE_STORE_HPP
```

#### Review & Status
- ok

---

### Change 2: Nowy plik `include/publisher/runtime/publisher_runtime.hpp`

**What:** Trzy explicit specializations `PublisherRuntime<SinkKind>` — każda z metodą statyczną `publish()` realizującą pełny pipeline po stronie publishera:
1. resolve token → `OutputChannel`
2. indeks → pobranie uchwytu z `store`
3. `obj.payload()` → dane (jedyny kontrakt z `Derived`)
4. zapis do uchwytu

**Why:**
- `Derived` dostarcza tylko `std::string_view payload() const noexcept` — nic nie wie o sinkach ani handle'ach.
- Cały routing, wybór zasobu i zapis leży po stronie `PublisherRuntime` — właściwy podział odpowiedzialności.
- Primary template niezdefiniowany — nieznany `SinkKind` = błąd kompilacji.
- `publish()` jest `noexcept`. Null-check na handle przez `assert()` — zero kosztu w release.
- `SocketHandle` to aktualnie stub (`fakeFd`) — wywołanie `obj.payload()` pominięte do czasu implementacji.

**Kontrakt `Derived` (jedyny wymagany):**
```cpp
std::string_view payload() const noexcept;
```

**Impact:** Hot path — zero overhead w release. Pipeline w całości po stronie publishera.

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
- [ ] Awaiting review

---

### Change 3: `CMakeLists.txt` — dodanie dwóch nowych nagłówków do `add_executable`

**What:** Dodanie `resource_store.hpp` i `publisher_runtime.hpp` do listy źródeł `MyApp` (tylko dla IDE).

**Why:** Spójność z pozostałymi nagłówkami już tam wpisanymi.

**Impact:** Brak wpływu na build.

**Proposed code:**
```cmake
add_executable(MyApp src/main.cpp
        include/publisher/core/publisher_types.hpp
        include/publisher/core/publish_token.hpp
        include/publisher/runtime/sink_handles.hpp
        include/publisher/runtime/token_registry.hpp
        include/publisher/runtime/registration_handle.hpp
        include/publisher/runtime/resource_store.hpp
        include/publisher/runtime/publisher_runtime.hpp
        include/publisher/api/publishable.hpp)
```

#### Review & Status
- [ok ] Awaiting review

---

## Kontrakt `Derived`

`PublisherRuntime` wymaga od `Derived` jednej metody:

```cpp
std::string_view payload() const noexcept;
```

`Derived` nie zna sinków, handle'i ani zasobów runtime. Pełna separacja odpowiedzialności.

---

## Build Errors (if any)

_Populated after build._

---

## Test Results (if any)

_Populated after tests._
