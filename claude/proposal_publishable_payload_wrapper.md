# Proposal: Oddzielenie PayloadBase od Publishable — wrapper PublishablePayload

**Date:** 2026-04-04

---

## Original Prompt

Błędy build wynikające z tego, że `PayloadBase` dziedziczy po `Publishable<Derived>`.
Rozwiązanie: usunąć dziedziczenie, dodać oddzielny wrapper `PublishablePayload<Payload>`.

---

## Context

### Dwa niezależne pipeline'y

W kodzie istnieją dwie osobne ścieżki danych:

**Stary pipeline (LogEngine):**
```
Handler::log<Tag>(...args)
  → pack_header_args → Builder::build<Tag>(tuple) → P payload{}
  → engine.enqueue(payload) → LogRecord::storage[256B]
  → worker_loop → submit_fn → adapter(env.debug_print) → Publisher<Policy, Sink>
```
`Builder::build` wymaga `P payload{}` — default construction. Payload jest czystą daną, leci do fixed 256B storage.

**Nowy pipeline (PublisherRuntime):**
```
Publishable::publish<Sink>(registry, store)
  → PublisherRuntime<Sink>::publish(registry, store, token, obj)
  → SinkTraits<Sink>::write(handle, obj.payload())
```
Osobna ścieżka — nie przechodzi przez `LogEngine`. Wymaga `RegistrationHandle` i `payload()`.

### Dlaczego PayloadBase NIE powinien dziedziczyć po Publishable

`PayloadBase` żyje w starym pipeline:
- **Default constructible** — `Builder::build` robi `P payload{}`, testy robią `Base b{}`
- **Trivially copyable** — test `static_assert(std::is_trivially_copyable_v<RequestPayload>)`
- **Aggregate** — pola X-macro, padding, constexpr tag

`Publishable<Derived>` łamie oba kontrakty:
- `explicit Publishable(RegistrationHandle)` — suppresuje default constructor
- `RegistrationHandle` ma user-defined destruktor → nie jest trivially copyable

**Wniosek:** Łączenie ich w hierarchię dziedziczenia jest błędem architektonicznym. To dwa różne światy z różnymi kontraktami.

### Poprawny podział

```
PayloadBase<Tag, Derived>              → czysta dana, POD, trivially copyable, stary pipeline
PublishablePayload<Payload>            → wrapper: dane + RegistrationHandle, nowy pipeline
```

---

## Proposed Changes

### Change 1: `include/common/messages/payloads/payloads.hpp` — usunięcie dziedziczenia po `Publishable`

**What:**
- Usunąć `#include "publisher/api/publishable.hpp"` i `#include "publisher/runtime/registration_handle.hpp"`
- Usunąć `: public publisher::api::Publishable<Derived>` z `PayloadBase`
- Usunąć `using PublishableBase = ...`
- Usunąć `explicit PayloadBase(RegistrationHandle handle)` constructor

**Why:** `PayloadBase` jest agregatem danych w starym pipeline.
Przywracamy trivial copyability, default construction, i czystość typów — wszystkie testy i `Builder::build` wracają do stanu sprzed.

**Impact:** Przywrócenie poprawnego zachowania. Zero wpływu na nowy pipeline publishera.

**Proposed code:**
```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>

#include "common/messages/log_message.hpp"

template<std::size_t N>
struct Padding
{
    std::byte padding[N];
};

template<>
struct Padding<0>
{
};

enum class Severity : std::uint8_t { Info, Warn, Error };

inline std::ostream& operator<<(std::ostream& os, Severity s)
{
    switch (s)
    {
        case Severity::Info:  return os << "Info";
        case Severity::Warn:  return os << "Warn";
        case Severity::Error: return os << "Error";
    }

    return os << "Unknown(" << static_cast<int>(static_cast<std::uint8_t>(s)) << ')';
}

#ifndef MSG_PAD_TO_64B
#define MSG_PAD_TO_64B 1
#endif

template<MsgTag Tag, typename Derived>
struct PayloadBase
{
    static constexpr MsgTag type_id = Tag;

    #define X(C,F) C F;
    #include "common/messages/payloads/log_payloads.def"
    #undef X

#if MSG_PAD_TO_64B
    struct _FieldsSizeProbe
    {
        #define X(C,F) C F;
        #include "common/messages/payloads/log_payloads.def"
        #undef X
    };

    static constexpr std::size_t _fields_size = sizeof(_FieldsSizeProbe);

    static constexpr std::size_t _pad_bytes = []() constexpr
    {
        if constexpr (Tag == MsgTag::Generic)
        {
            return (_fields_size < 64u) ? (64u - _fields_size) : 0u;
        }
        else
        {
            return 0u;
        }
    }();

    [[no_unique_address]] Padding<_pad_bytes> _padding{};

    static_assert(Tag != MsgTag::Generic || _fields_size <= 64u,
                  "Generic PayloadBase fields exceed 64 bytes");
#else
    static constexpr std::size_t _pad_bytes = 0;
#endif

    constexpr MsgTag tag() const noexcept
    {
        return type_id;
    }

    void print_header(std::ostream& os) const
    {
        os << "[tag=" << static_cast<int>(type_id) << "] ";

        #define X(C,F) os << #F << '=' << F << ' ';
        #include "common/messages/payloads/log_payloads.def"
        #undef X
    }

    void debug_print(std::ostream& os) const
    {
        static_cast<const Derived*>(this)->debug_impl(os);
    }
};

template<MsgTag Tag, typename Derived>
std::ostream& operator<<(std::ostream& os, const PayloadBase<Tag, Derived>& p)
{
    p.debug_print(os);
    return os;
}
```

#### Review & Status
- - [ ok] Awaiting review

---

### Change 2: Nowy plik `include/publisher/api/publishable_payload.hpp` — wrapper/koperta

**What:** `PublishablePayload<Payload>` — wrapper (koperta) łącząca czysty payload z `RegistrationHandle`. Dziedziczy po `Publishable<PublishablePayload<Payload>>`.

**Why:**
- Nowy pipeline publishera wymaga `RegistrationHandle` + `publish<Sink>()`. Ten kontrakt żyje w wrapperze, NIE w `PayloadBase`.
- `PayloadBase` pozostaje czystą daną — nie zna publishera.
- Wrapper tworzy się na cold path (setup/rejestracja). Na hot path wywołuje się `publish<Sink>()`.
- `data()` — const i non-const accessor do wewnętrznego payloadu (np. ustawianie pól po wrappingu).
- `payload()` serializacja — **TBD**, osobna decyzja. Gdy kontrakt payloadu będzie ustalony, wrapper go zaimplementuje. Na teraz zostawiony jako komentarz.
- `static_assert` na `sizeof(Payload)` i `!is_polymorphic_v<Payload>` — wymuszenie kontraktów z CLAUDE.md po stronie compile-time.

**Lifecycle wrappera:**
```
1. TokenRegistry registry;
2. RegistrationHandle handle(registry, OutputChannel::Channel0);
3. RequestPayload data{};  // czysta dana — stary pipeline
4. PublishablePayload<RequestPayload> envelope(std::move(handle), std::move(data));
5. envelope.data().severity = Severity::Error;  // non-const data()
6. envelope.publish<SinkKind::Terminal>(registry, store);  // nowy pipeline
```

**Impact:** Nowy typ w `publisher::api`. Zero wpływu na istniejący kod — Builder, Engine, testy nie zmieniają się.

**Proposed code:**
```cpp
//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHABLE_PAYLOAD_HPP
#define MYSERVER_PUBLISHABLE_PAYLOAD_HPP

#include <type_traits>
#include <utility>

#include "publisher/api/publishable.hpp"
#include "publisher/runtime/registration_handle.hpp"

namespace publisher::api
{
    template<typename Payload>
    class PublishablePayload : public Publishable<PublishablePayload<Payload>>
    {
        static_assert(sizeof(Payload) <= 256,
                      "Payload exceeds 256B limit");
        static_assert(!std::is_polymorphic_v<Payload>,
                      "Virtual dispatch is forbidden");

    public:
        PublishablePayload(publisher::runtime::RegistrationHandle handle, Payload payload)
            : Publishable<PublishablePayload<Payload>>(std::move(handle))
            , payload_(std::move(payload))
        {}

        [[nodiscard]] const Payload& data() const noexcept
        {
            return payload_;
        }

        [[nodiscard]] Payload& data() noexcept
        {
            return payload_;
        }

        // std::string_view payload() const noexcept;
        // — TBD: serialization contract not yet established.
        // When defined, called by PublisherRuntime<Sink>::publish()
        // via SinkTraits<Sink>::write(handle, obj.payload()).

    private:
        Payload payload_;
    };
} // namespace publisher::api

#endif // MYSERVER_PUBLISHABLE_PAYLOAD_HPP
```

#### Review & Status
- - [ ok] Awaiting review

---

## Build Errors (if any)

_Populated after build._

---

## Test Results (if any)

_Populated after tests._
