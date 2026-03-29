# ✅ CLOSED
# Proposal: Eliminate heap allocation in submit_impl

**Date:** 2026-03-29

---

## Original Prompt

> tak (przygotuj proposal dla punktu 3 z listy LogRecord issues)

---

## Context

`submit_impl` jest wywoływany przez worker thread na każdym przetworzonym rekordzie — to hot path.

Obecny kod:
```cpp
AdapterFn adapter = [](const auto& envelope) -> std::string_view {
    thread_local std::string buffer;
    buffer.clear();

    std::ostringstream oss;      // ← HEAP ALLOC: nowy obiekt przy każdym wywołaniu
    envelope.debug_print(oss);
    buffer = oss.str();          // ← HEAP ALLOC: kopiowanie do buffer

    return std::string_view{buffer};
};
```

`std::ostringstream` tworzy wewnętrzny `std::string` przy każdej konstrukcji. Mimo że `thread_local std::string buffer` jest reużywany, `oss` jest lokalny — alokuje i dealokuje przy każdym rekordzie.

**Rozwiązanie:** własny `streambuf` piszący wprost do tablicy `char[]` na stosie / w `thread_local`. Zero alokacji, `debug_print` pisze do `std::ostream&` jak dotychczas — interfejs payloadów bez zmian.

---

## Proposed Changes

### Change 1: `include/logger/core/stream_adapter.hpp` — dodaj `FixedStringBuf<N>`

**What:** Dodać template `FixedStringBuf<N>` — lekki `std::streambuf` piszący do wewnętrznej tablicy `std::array<char, N>`. Brak dziedziczenia wirtualnych metod poza `overflow` (wymagane przez `std::streambuf`).

**Why:** Daje `std::ostream`-kompatybilny cel zapisu bez jakiejkolwiek alokacji sterty. Pasuje do istniejącego pliku — `stream_adapter.hpp` to już miejsce na infrastrukturę streamów.

**Impact:** Zerowa alokacja na hot path. Rozmiar bufora jest compile-time — można dobrać do maksymalnej długości wiadomości. Brak wpływu na istniejące użycia `StreamAdapter`.

**Proposed code** (dodać przed definicją `StreamAdapter`):
```cpp
// Fixed-size streambuf — writes into an internal char array.
// No heap allocation. Overflow (message too long) truncates silently.
template<std::size_t N>
struct FixedStringBuf : std::streambuf
{
    FixedStringBuf()  { setp(buf_.data(), buf_.data() + N); }
    void reset()      { setp(buf_.data(), buf_.data() + N); }

    std::string_view view() const noexcept
    {
        return {buf_.data(), static_cast<std::size_t>(pptr() - pbase())};
    }

private:
    // Silently drop bytes that exceed the buffer.
    int_type overflow(int_type ch) override
    {
        return ch; // consume but discard
    }

    std::array<char, N> buf_{};
};
```

#### Review & Status
-[ok ] Awaiting review

---

### Change 2: `include/logger/core/log_engine.hpp` — przepisz adapter w `submit_impl`

**What:** Zastąpić lokalny `std::ostringstream oss` + `thread_local std::string buffer` parą `thread_local` obiektów: `FixedStringBuf<1024>` + `std::ostream`. Rozmiar 1024B jest z dużym zapasem wobec limitu 256B payloadu.

**Why:** Eliminuje alokację heapową na każdym rekordzie. `thread_local` gwarantuje że jeden obiekt na wątek — inicjalizowany raz, reużywany przy każdym wywołaniu po `reset()`.

**Impact:** Zero alokacji na hot path. `debug_print` otrzymuje ten sam `std::ostream&` co dotychczas — zero zmian w payloadach. `string_view` zwracany z adaptera wskazuje wprost na bufor `thread_local` — lifetime bezpieczny w kontekście wywołania `Pub::publish`.

**Proposed code:**
```cpp
template<typename Stored>
static void submit_impl(void* storage)
{
    auto* obj = static_cast<Stored*>(storage);
    auto& env = obj->env;

    using Envelope  = std::decay_t<decltype(env)>;
    using AdapterFn = std::string_view (*)(const Envelope&);

    AdapterFn adapter = [](const Envelope& envelope) -> std::string_view {
        thread_local FixedStringBuf<1024> buf;
        thread_local std::ostream os(&buf);
        buf.reset();
        os.clear();

        envelope.debug_print(os);
        return buf.view();
    };

    using Pub = Publisher<TerminalPolicy, TextSink>;
    Pub::publish(env, adapter);
}
```

#### Review & Status
-[ok ] Awaiting review

---

## Build Errors (if any)

_Brak — do uzupełnienia po buildzie._

---

## Test Results (if any)

_Brak — do uzupełnienia po uruchomieniu testów._
