# Proposal: Oddzielenie PayloadBase od Publishable — wrapper PublishablePayload

**Date:** 2026-04-04

---

## Original Prompt

Błędy build wynikające z tego, że `PayloadBase` dziedziczy po `Publishable<Derived>`.
Rozwiązanie: usunąć dziedziczenie, dodać oddzielny wrapper `PublishablePayload<Payload>`.

---

## Context

`PayloadBase` jest czystą daną — musi być:
- default-constructible (`P payload{}`, `Base b{}`)
- trivially copyable (`static_assert(std::is_trivially_copyable_v<RequestPayload>)`)

`Publishable<Derived>` łamie oba te kontrakty:
- `explicit Publishable(RegistrationHandle)` — brak default constructora
- `RegistrationHandle` ma user-defined destruktor → nie jest trivially copyable

**Podział odpowiedzialności:**
```
PayloadBase<Tag, Derived>          → czysta dana, POD, trivially copyable
PublishablePayload<Payload>        → wrapper do publishowania: dane + RegistrationHandle
```

---

## Proposed Changes

### Change 1: `include/common/messages/payloads/payloads.hpp` — usunięcie dziedziczenia po `Publishable`

**What:**
- Usunąć `#include "publisher/api/publishable.hpp"` i `#include "publisher/runtime/registration_handle.hpp"`
- Usunąć `: public publisher::api::Publishable<Derived>` z `PayloadBase`
- Usunąć `using PublishableBase = ...`
- Usunąć `explicit PayloadBase(RegistrationHandle handle)` constructor

**Why:** `PayloadBase` jest agregatem danych. Nie powinien znać publishera, tokenu ani handle'a.
Przywracamy trivial copyability i default construction — wszystkie testy wracają do stanu sprzed.

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
- [ ] Awaiting review

---

### Change 2: Nowy plik `include/publisher/api/publishable_payload.hpp` — wrapper

**What:** `PublishablePayload<Payload>` — łączy dane (`Payload`) z `RegistrationHandle`.
Dziedziczy po `Publishable<PublishablePayload<Payload>>`. Udostępnia dane przez `data()`.

**Why:**
- Publishowanie wymaga `RegistrationHandle` — żyje w tym wrapperze, nie w `PayloadBase`.
- `PayloadBase` pozostaje czystą daną.
- Wrapper jest tworzony tylko gdy faktycznie potrzeba publishować — cold path (setup).
- `payload()` — forma serializacji danych do `std::string_view` jest **otwartym pytaniem** (osobna decyzja, niezależna od tej zmiany).

**Impact:** Nowy typ, zero wpływu na istniejący kod.

**Proposed code:**
```cpp
//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHABLE_PAYLOAD_HPP
#define MYSERVER_PUBLISHABLE_PAYLOAD_HPP

#include <utility>

#include "publisher/api/publishable.hpp"
#include "publisher/runtime/registration_handle.hpp"

namespace publisher::api
{
    // PublishablePayload<Payload> — wraps a pure data payload with a RegistrationHandle.
    //
    // Responsibilities:
    //   - owns the RegistrationHandle (token lifecycle)
    //   - provides access to the wrapped payload via data()
    //   - payload() serialization contract is TBD
    //
    // Payload contract:
    //   - must be moveable
    //   - payload() — to be defined when serialization contract is established
    template<typename Payload>
    class PublishablePayload : public Publishable<PublishablePayload<Payload>>
    {
    public:
        PublishablePayload(publisher::runtime::RegistrationHandle handle, Payload payload)
            : Publishable<PublishablePayload<Payload>>(std::move(handle))
            , payload_(std::move(payload))
        {}

        [[nodiscard]] const Payload& data() const noexcept
        {
            return payload_;
        }

        // std::string_view payload() const noexcept;
        // — TBD: serialization contract not yet established

    private:
        Payload payload_;
    };
} // namespace publisher::api

#endif // MYSERVER_PUBLISHABLE_PAYLOAD_HPP
```

#### Review & Status
- [ ] Awaiting review

---

## Build Errors (if any)

_Populated after build._

---

## Test Results (if any)

_Populated after tests._
