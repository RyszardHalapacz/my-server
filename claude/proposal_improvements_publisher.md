# Proposal: Publisher + LogEngine — analiza słabych stron i propozycje poprawek

**Date:** 2026-04-05

---

## Original Prompt

> Przeanalizuj repo bardzo dokładnie pod kątem publishera, znajdź mi wszystkie słabe jego strony,
> znajdź mi testy do tego fragmentu testy jednostkowe.
> Wypisz mi to w formie listy gdzie punktami są poszczególne problemy a podpunktami propozycje zmian kodu i testów.
>
> Zrób to jeszcze raz pod kątem LogEngine'a — singleton, architektura, wydajność, każdy kąt.

---

## Context

### Publisher

Publisher składa się z dwóch warstw:
- **Classic layer** (`publisher.hpp`, `policy_publisher.hpp`, `sink_publisher.hpp`) — nieużywana w produkcji, tylko testy
- **Runtime layer** (`publisher/runtime/*`) — aktywna, użyta w `LogEngine::submit_impl` i `main.cpp`

Testy publishera:
- `test/publisher/publisher_test.cpp` — 4 testy classic layer
- `test/publisher/runtime/publisher_runtime_test.cpp` — 15 testów runtime layer

### LogEngine

LogEngine to singleton zarządzający asynchronicznym logowaniem:
- **Core:** `include/logger/core/log_engine.hpp` + `src/logger/core/log_engine.cpp`
- **Lock-free queue:** `include/logger/core/lockfree_queue.hpp` (MPSC queue + Treiber stack FreeList)
- **Record pool:** `include/logger/core/log_record.hpp` (alignas(64), 256B storage, function pointer erasure)
- **Stream adapter:** `include/logger/core/stream_adapter.hpp` (FixedStringBuf, EnvelopeLike concept)
- **Public API:** `include/logger/logger.hpp` (Handler::log → enqueue)

Testy LogEngine:
- `test/logger/core/log_engine_pipeline_test.cpp` — 4 testy (VerifiableEngine, nie singleton)
- `test/logger/core/log_record_test.cpp` — alignment, size
- `test/logger/core/mpsc_queue_test.cpp` — push/pop/FIFO/empty
- `test/logger/core/stream_adapter_test.cpp` — EnvelopeLike concept
- `test/stress/log_engine_stress_test.cpp` — 4 konfiguracje multi-threaded (StressableLogEngine, nie singleton)
- `test/stress/mpsc_queue_stress.hpp` — concurrent queue stress
- `test/logger/mocks/mock_log_engine.hpp` — TestLogEngine (osobna klasa, nie mock singletona)

Architektura data flow:
```
Producer threads              Worker thread (single)
───────────────────────────────────────────────────
enqueue(Envelope)
  ├─ ensure_running()         (first call → start worker)
  ├─ freelist_.try_pop()      → CAS on FreeList
  ├─ placement new(storage)
  ├─ assign destroy_fn/submit_fn
  └─ queue_.push(rec)         → tail_.exchange (MPSC)
                                        │
                                        ↓
                              worker_loop()
                                ├─ queue_.pop()
                                ├─ submit_fn(storage)
                                │   ├─ debug_print → FixedStringBuf
                                │   └─ PublisherRuntime::publish_view()
                                ├─ destroy_fn(storage)
                                └─ freelist_.push(rec)  → recycle
```

---

## Publisher — lista problemów

---

### 1. [CRITICAL] Assert-only null guards w `SinkTraits::write()` — UB w Release

**Pliki:** `include/publisher/runtime/sink_traits.hpp:24-28, 36-40, 48-54`

**Problem:** Jedynym zabezpieczeniem przed dereferencją `nullptr` w `write()` jest `assert()`.
W Release builds (`-DNDEBUG`) assert się kompiluje do niczego — dostajemy **undefined behavior**
(null pointer dereference) na hot path bez jakiejkolwiek diagnostyki.

```cpp
// Obecny kod (linia 24-28):
static void write(handle_type& handle, std::string_view data) noexcept
{
    assert(handle.out != nullptr && "TerminalHandle: null stream");  // ← znika w Release
    handle.out->write(data.data(), static_cast<std::streamsize>(data.size()));  // ← UB
}
```

**Testy:** Istniejące testy (`SinkTraitsTerminalTest`, `SinkTraitsFileTest`) pokrywają tylko happy path
z poprawnie zainicjalizowanymi handle'ami. Brak testów dla niezainicjalizowanego handle'a.

- **Propozycja zmian kodu:**
  - Zamienić `assert` na compile-time enforceable contract LUB dodać `if constexpr`/`if` guard z early return
  - Opcja A (defensive, minimalna): dodać `if (!handle.out) return;` przed dereferencją
  - Opcja B (strict, preferowana): zostawić assert jako dokumentację, ale **dodać** `[[assume(handle.out != nullptr)]]` (C++23) lub po prostu udokumentować precondition i zagwarantować go w `OutputResourceStore` — np. `validate()` method wywoływana raz po inicjalizacji (cold path)
  - Analogicznie dla `FileHandle` i `SocketHandle`

- **Propozycja testów:**
  - Test `SinkTraitsTerminalTest::WriteToNullHandleDoesNotCrash` (jeśli opcja A — defensive guard)
  - Test `OutputResourceStoreTest::ValidateDetectsNullTerminalHandle`
  - Test `OutputResourceStoreTest::ValidateDetectsNullFileHandle`

---

### 2. [CRITICAL] Assert-only validation w `TokenRegistry::resolve()` — UB w Release

**Plik:** `include/publisher/runtime/token_registry.hpp:69-74`

**Problem:** `resolve()` jest oznaczone `noexcept` i jedyną walidację stanowią `assert`y.
W Release: nieprawidłowy token → odczyt z niezainicjalizowanego elementu `tokenToChannel_[]` → UB.
`resolve()` jest wywoływane na **hot path** (z `publish_view()`).

```cpp
// Obecny kod (linia 69-74):
[[nodiscard]] OutputChannel resolve(PublishToken token) const noexcept
{
    assert(isValidToken(token) && "Invalid publish token");     // ← znika w Release
    assert(tokenUsed_[token.value] && "Inactive publish token"); // ← znika w Release
    return tokenToChannel_[token.value];                         // ← OOB jeśli token.value >= nextToken_
}
```

- **Propozycja zmian kodu:**
  - Opcja A: dodać `if (!isValidToken(token)) return OutputChannel::Channel0;` (safe fallback)
  - Opcja B (preferowana): zostawić asserty, ale dodać `validate_token()` method na cold path
    wywoływaną np. w `RegistrationHandle` constructor — gwarancja, że do hot path
    docierają TYLKO valid tokens. Token pochodzi z `acquire()` więc invariant jest naturalny,
    ale warto go udokumentować i przetestować edge case'y
  - Dodać `static_assert` na rozmiar tablicy: `static_assert(kMaxBindings <= std::numeric_limits<uint32_t>::max())`

- **Propozycja testów:**
  - Test `TokenRegistryTest::ResolveWithMaxValueTokenIsHandled`
  - Test `TokenRegistryTest::AcquireRelease256TokensAndResolve` — full cycle stress test
  - Death test (Debug only): `EXPECT_DEBUG_DEATH(reg.resolve(kInvalidToken), "Invalid publish token")`

---

### 3. [CRITICAL] Brak bounds check w `toIndex()` + brak walidacji w `publish_view()`

**Pliki:** `include/publisher/core/publisher_types.hpp:28-31`, `include/publisher/runtime/publisher_runtime.hpp:29-33`

**Problem:** `toIndex()` to surowy `static_cast<size_t>(channel)` bez walidacji.
Jeśli `resolve()` zwróci `OutputChannel::Count` (wartość 4) lub inną nieprawidłową wartość,
to `store.terminals[idx]` jest **out-of-bounds** access — `OutputResourceStore` ma tablice o rozmiarze `kChannelCount` = 4 (indeksy 0-3).

```cpp
// publisher_types.hpp:28
constexpr std::size_t toIndex(OutputChannel channel) noexcept {
    return static_cast<std::size_t>(channel);  // ← brak sprawdzenia czy < Count
}

// publisher_runtime.hpp:29-33 (hot path):
const auto channel = registry.resolve(token);
const auto idx = publisher::core::toIndex(channel);
auto& handle = store.terminals[idx];  // ← OOB jeśli idx >= kChannelCount
```

- **Propozycja zmian kodu:**
  - Dodać `assert(static_cast<std::size_t>(channel) < static_cast<std::size_t>(OutputChannel::Count))` w `toIndex()`
  - Alternatywnie: zmienić `std::array` w `OutputResourceStore` na wrapper z bounds-checking accessorem

- **Propozycja testów:**
  - `static_assert(toIndex(OutputChannel::Channel3) == 3)` — compile-time sanity
  - `static_assert(toIndex(OutputChannel::Channel0) == 0)` — compile-time sanity
  - Test `OutputResourceStoreTest::ChannelCountMatchesEnumCount`

---

### 4. [HIGH] Brak gwarancji exclusive ownership channelu (slotu)

**Pliki:** `include/publisher/runtime/token_registry.hpp`, `include/publisher/runtime/registration_handle.hpp`

**Problem:** Obecny model gwarantuje unikatowość tokenu, ale **nie gwarantuje unikatowości channelu**.
Możliwe jest równoczesne przypisanie wielu tokenów do tego samego `OutputChannel`:

```cpp
TokenRegistry reg;
auto t0 = reg.acquire(OutputChannel::Channel0);  // OK
auto t1 = reg.acquire(OutputChannel::Channel0);  // ← też OK! Brak błędu.
```

To jest sprzeczne z docelową semantyką systemu:
- 1 logger/publisher object → 1 channel
- channel → zasób ekskluzywny
- brak współdzielenia by default

W efekcie:
- wiele obiektów może pisać przez ten sam channel bez synchronizacji
- concurrency assumptions mogą zostać złamane (wiele writerów → interleaved output)
- `OutputResourceStore` indeksuje po channel — wielu writerów uderza w ten sam handle

```cpp
// token_registry.hpp:24-46 — acquire() nie sprawdza czy channel jest już zajęty:
[[nodiscard]] PublishToken acquire(OutputChannel channel)
{
    // ... token allocation ...
    tokenToChannel_[token.value] = channel;  // ← nie sprawdza unikalności channelu
    tokenUsed_[token.value] = true;
    return token;
}
```

- **Propozycja zmian kodu:**
  - Dodać tracking zajętości channeli w `TokenRegistry`:
    ```cpp
    std::array<bool, kChannelCount> channelUsed_{};
    ```
  - W `acquire(channel)`:
    - Sprawdzić `channelUsed_[toIndex(channel)]`
    - Jeśli `true` → error (assert, exception lub `std::optional<PublishToken>`)
    - Jeśli `false` → oznaczyć jako zajęty
  - W `release(token)`:
    - Resolve token → channel
    - Oznaczyć `channelUsed_[toIndex(channel)] = false`
  - Udokumentować invariant: *channel jest zasobem exclusive*
  - (Opcjonalnie) zmienić nazwę: `TokenRegistry` → `BindingRegistry`

- **Propozycja testów:**
  - `TokenRegistryTest::AcquireSameChannelTwiceFails`
  - `RegistrationHandleTest::ChannelBecomesFreeAfterRelease`
  - `PublisherRuntimeTest::ExclusiveChannelOwnershipEnforced`

---

### 5. [HIGH] Brak jawnego i wymuszonego modelu concurrency dla publish path

**Pliki:** `include/publisher/runtime/publisher_runtime.hpp`, `sink_traits.hpp`, `resource_store.hpp`

**Problem:** Kod nie definiuje jasno, czy `publish_view()`:
- jest wywoływane z jednego wątku (worker LogEngine),
- czy może być wywoływane z wielu wątków równolegle.

To jest krytyczne, bo wpływa na:
- poprawność (race conditions na handle)
- ordering logów (interleaved writes)
- thread-safety sinków (`std::ostream::write` nie jest atomic)
- wymagania wobec OutputResourceStore (czy potrzebna synchronizacja?)

Obecny stan: `publish_view()` nie ma żadnej adnotacji ani komentarza kontraktowego.
W praktyce jest wywoływane z **jednego** worker thread'a (LogEngine), ale to nigdzie nie jest udokumentowane
ani wymuszone. Nic nie blokuje wywołania z wielu wątków.

- **Propozycja zmian kodu:**
  - Jawnie ustalić model:
    - **Opcja A (preferowana):** `publish_view()` jest wywoływane wyłącznie z jednego worker thread.
      Producenci → queue → worker → publish. Dodać komentarz kontraktowy:
      ```cpp
      // Contract: publish_view() MUST be called from a single consumer thread.
      // Concurrent calls from multiple threads are undefined behavior.
      ```
    - **Opcja B:** Multi-thread publish dozwolony tylko dla rozłącznych channeli.
      Wymaga dodatkowej dokumentacji i testów
  - (Future) Opcja C: Wymusić thread affinity — assert na thread_id w Debug

- **Propozycja testów:**
  - Zależne od wybranego modelu concurrency
  - Jeśli opcja A: test dokumentacyjny / komentarzowy (compile-time), brak runtime testu
  - Jeśli opcja B: stress test z wieloma wątkami na rozłącznych channelach

---

### 6. [HIGH] Channel ≠ Resource — brak jawnego modelu (exclusive vs shared)

**Pliki:** `include/publisher/runtime/resource_store.hpp`, `include/publisher/runtime/sink_traits.hpp`

**Problem:** Channel wygląda jak prywatny kanał obiektu, ale:
- `OutputResourceStore` indeksuje handle'e po channel index
- nic nie zabrania wskazania wielu channeli na ten sam zasób (np. `&std::cout`)
- `LogEngine::store()` (log_engine.cpp:20-23) przypisuje `&std::cout` do **wszystkich 4 channeli**:

```cpp
// log_engine.cpp:20-23:
s.terminals[toIndex(OutputChannel::Channel0)].out = &std::cout;
s.terminals[toIndex(OutputChannel::Channel1)].out = &std::cout;  // ← shared resource!
s.terminals[toIndex(OutputChannel::Channel2)].out = &std::cout;  // ← shared resource!
s.terminals[toIndex(OutputChannel::Channel3)].out = &std::cout;  // ← shared resource!
```

To prowadzi do niejednoznaczności:
- unikatowy channel ≠ unikatowy resource
- brak jasnego modelu synchronizacji dla shared resources
- jeśli dwa publisher'y piszą na różne channele ale ten sam `std::cout` — interleaved output

**Decyzja architektoniczna wymagana:**
- Default: exclusive resource per channel (1:1)
- Shared resource: opcjonalne rozszerzenie z jawną synchronizacją

- **Propozycja zmian kodu:**
  - Udokumentować invariant:
    ```cpp
    // Default invariant:
    // Each OutputChannel owns a unique resource (no sharing).
    // Shared resources require explicit synchronization (not yet supported).
    ```
  - W `OutputResourceStore`: wymusić 1:1 channel → resource (validate na cold path)
  - (Future extension): mapping `channel → resourceId → handle` umożliwiający sharing z lockiem

- **Propozycja testów:**
  - Do ustalenia po decyzji architektonicznej
  - Jeśli 1:1: `OutputResourceStoreTest::DuplicateResourceAcrossChannelsFails`

---

### 7. [HIGH] Brak `static_assert(!std::is_polymorphic_v<T>)` na typach publishera

**Pliki:** Wszystkie typy w `include/publisher/`

**Problem:** CLAUDE.md wymaga: *"No runtime polymorphism: `static_assert(!std::is_polymorphic_v<T>, ...)`"*.
Żaden typ publishera nie ma tego assertu. Co prawda żaden nie używa virtual functions,
ale brak compile-time enforcement oznacza, że ktoś może przypadkowo dodać `virtual` w przyszłości
bez złamania builda.

**Dotyczy typów:**
- `PublishToken` (publish_token.hpp)
- `TerminalHandle`, `FileHandle`, `SocketHandle` (sink_handles.hpp)
- `TokenRegistry` (token_registry.hpp)
- `RegistrationHandle` (registration_handle.hpp)
- `OutputResourceStore` (resource_store.hpp)
- `PolicyBase`, `TerminalPolicy`, `FilePolicy` (policy_publisher.hpp)
- `SinkBase`, `JsonSink`, `TextSink` (sink_publisher.hpp)

**Wpływ na system:** Compile-time only. Zero runtime impact. Prewencja na przyszłość.

- **Propozycja zmian kodu:**
  - Dodać w każdym pliku nagłówkowym, pod definicją typu:
    ```cpp
    static_assert(!std::is_polymorphic_v<PublishToken>, "Virtual dispatch forbidden");
    ```
  - Priorytet: runtime layer (hot path) > classic layer

- **Propozycja testów:**
  - Nie potrzeba — to są compile-time checks, test = kompilacja

---

### 8. [HIGH] `FilePolicy` — brak deleted copy, implicit copy = UB z `std::ofstream`

**Plik:** `include/publisher/policy_publisher.hpp:67-92`

**Problem:** `FilePolicy` posiada member `std::ofstream file_` (linia 91).
Klasa NIE deklaruje copy constructor / copy assignment jako `= delete`.
W C++17+ `std::ofstream` jest noncopyable więc implicit copy nie skompiluje się,
ale brakuje explicit design intent. Brak `= default` move operations.

**Wpływ na system:** Classic layer, **nieużywany w produkcji**. Latent UB jeśli ktoś użyje.

```cpp
// Obecny stan: brak deklaracji copy/move
template<typename Sink>
struct FilePolicy : PolicyBase<FilePolicy<Sink>, Sink>
{
    explicit FilePolicy(std::string path = "PublisherFile.log") ...
private:
    std::ofstream file_;
};
```

- **Propozycja zmian kodu:**
  ```cpp
  FilePolicy(const FilePolicy&) = delete;
  FilePolicy& operator=(const FilePolicy&) = delete;
  FilePolicy(FilePolicy&&) = default;
  FilePolicy& operator=(FilePolicy&&) = default;
  ```

- **Propozycja testów:**
  - Compile-time: `static_assert(!std::is_copy_constructible_v<FilePolicy<JsonSink>>)`
  - Compile-time: `static_assert(std::is_move_constructible_v<FilePolicy<JsonSink>>)`

---

### 9. [HIGH] Hot-path `std::string` allocation w `PolicyBase::publish()`

**Plik:** `include/publisher/policy_publisher.hpp:29-42`

**Problem:** Classic Publisher layer alokuje `std::string` na każde wywołanie `publish()`:

```cpp
void publish(view_type line) {
    std::string msg = sink_type::format(line);  // ← heap allocation
    view_type msg_view{msg.data(), msg.size()};
    static_cast<Derived*>(this)->write_impl(msg_view);
}
```

Narusza CLAUDE.md: *"Heap allocations are FORBIDDEN on hot path"*.

**Wpływ na system:** Classic layer, **nieużywany w produkcji**. Naruszenie constraintu jeśli ktoś użyje.

- **Propozycja zmian kodu:**
  - Opcja A: Oznaczyć classic layer jako `[[deprecated("Use PublisherRuntime instead")]]`
  - Opcja B: Przerobić `format()` na zapis do pre-allocated bufora (np. `FixedStringBuf`)
  - Opcja C: Usunąć classic layer jeśli jest definitywnie zastąpiony

- **Propozycja testów:**
  - Jeśli opcja A: test kompilacji z `-Wdeprecated` flag
  - Jeśli opcja B: test `PolicyBaseTest::PublishDoesNotAllocateOnHeap` (trudny do zweryfikowania bez custom allocator)

---

### 10. [MEDIUM] Brak kontraktu lifetime dla `OutputResourceStore` vs publish operations

**Pliki:** `include/publisher/runtime/publisher_runtime.hpp`, `include/publisher/runtime/resource_store.hpp`

**Problem:** `PublisherRuntime::publish_view()` przyjmuje `OutputResourceStore&` i bezpośrednio
indeksuje tablice handle'i. Brakuje jawnego kontraktu:
- Czy store może być przebudowany w trakcie pracy worker thread'a?
- Czy handle'e mogą się zmienić po inicjalizacji?
- Co jeśli store jest zniszczony przed zakończeniem publish operations?

Obecnie store jest `static local` w LogEngine (log_engine.cpp:15-29) — żyje do końca programu.
Ale ten invariant nie jest udokumentowany i nie jest enforced dla callsitów spoza LogEngine.

**Wpływ na system:** Aktywny runtime. Potential use-after-free jeśli lifetime złamany.

- **Propozycja zmian kodu:**
  - Udokumentować invariant:
    ```cpp
    // Contract: OutputResourceStore MUST outlive all publish operations.
    // Handles MUST NOT be modified after the first call to publish_view().
    ```
  - Zabronić runtime rebindingu (na razie) — store jest immutable po inicjalizacji
  - (Future): mechanizm invalidacji lub versioning store

- **Propozycja testów:**
  - Sanitizer test (ASan): store destroyed before publish → use-after-free detected

---

### 11. [MEDIUM] `OutputResourceStore` — brak walidacji stanu po inicjalizacji

**Plik:** `include/publisher/runtime/resource_store.hpp`

**Problem:** `OutputResourceStore` to plain struct z publicznymi tablicami.
Po inicjalizacji (cold path) handle'e mogą być `nullptr` / `{-1}`.
Nie ma żadnej metody `validate()` ani mechanizmu zapewniającego,
że store jest w poprawnym stanie zanim worker thread zacznie z niego czytać.

**Wpływ na system:** Aktywny runtime. Niezainicjalizowane handle → UB w SinkTraits (powiązane z #1).

- **Propozycja zmian kodu:**
  - Dodać `[[nodiscard]] bool validate(SinkKind kind) const noexcept` — cold path check
  - Wywołać `validate()` w `LogEngine::ensure_running()` lub podobnym punkcie startowym
  - Alternatywnie: builder pattern dla store'a z compile-time tracking (bardziej inwazyjne)

- **Propozycja testów:**
  - `OutputResourceStoreTest::DefaultConstructedHasNullTerminals`
  - `OutputResourceStoreTest::ValidateReturnsFalseForUninitialized`
  - `OutputResourceStoreTest::ValidateReturnsTrueAfterProperInit`

---

### 12. [MEDIUM] `RegistrationHandle::release()` — silent exception swallowing

**Plik:** `include/publisher/runtime/registration_handle.hpp:67-83`

**Problem:** `release()` łapie wszystkie wyjątki z `registry_->release(token_)` i je ignoruje:

```cpp
void release() noexcept
{
    if (registry_ != nullptr && token_.value != kInvalidToken.value)
    {
        try { registry_->release(token_); }
        catch (...) { }  // ← silent swallow
    }
    ...
}
```

Jest to zrozumiałe (destruktor nie może rzucać), ale powoduje, że np. double-release
nigdy nie zostanie wykryty w runtime. Błędy logiki programu są po cichu ignorowane.

- **Propozycja zmian kodu:**
  - Dodać `assert(false && "Unexpected exception in RegistrationHandle::release()")` w `catch` block
  - Lub: dodać logging do `catch` bloku (jeśli cold path pozwala)
  - Lub: wydzielić `try_release()` zwracający `bool`, a `release()` w destructor niech loguje fail

- **Propozycja testów:**
  - Test `RegistrationHandleTest::DoubleReleaseViaRawTokenDoesNotCorruptState`
  - Test: stwórz handle, ręcznie zwolnij token przez registry, potem zniszcz handle — nie powinno crashować

---

### 13. [MEDIUM] Socket sink — stub (no-op), dane znikają po cichu

**Plik:** `include/publisher/runtime/sink_traits.hpp:43-55`

**Problem:** `SinkTraits<SinkKind::Socket>::write()` nie robi nic:

```cpp
static void write(handle_type& handle, std::string_view data) noexcept
{
    assert(handle.fakeFd >= 0 && "SocketHandle: invalid fd");
    (void)handle;
    (void)data;
    // TODO: real socket write
}
```

Jeśli ktoś skonfiguruje `SinkKind::Socket`, dane po cichu znikną bez żadnego warning'u.

- **Propozycja zmian kodu:**
  - Opcja A: `static_assert(false, "Socket sink not implemented")` — ale to blokuje kompilację
  - Opcja B (preferowana): dodać `[[deprecated("Socket sink not yet implemented")]]` na `SinkTraits<SinkKind::Socket>`
  - Opcja C: usunąć `SinkKind::Socket` z enum dopóki nie jest zaimplementowany

- **Propozycja testów:**
  - Na razie brak — nie ma czego testować
  - Kiedy zaimplementowane: testy analogiczne do Terminal/File

---

### 14. [MEDIUM] Niepełne testy semantyki ownership `RegistrationHandle`

**Plik:** `include/publisher/runtime/registration_handle.hpp`

**Problem:** `RegistrationHandle` implementuje RAII + move semantics dla tokenów.
Istniejące testy pokrywają:
- ✅ `MoveSourceBecomesInvalid` — move ctor + moved-from state (`publisher_runtime_test.cpp:105-112`)
- ✅ `MoveAssignmentReleasesDestination` — move assignment (`publisher_runtime_test.cpp:114-123`)
- ✅ `TokenReleasedOnDestruction` — destruktor zwalnia token i umożliwia reuse (`publisher_runtime_test.cpp:93-103`)

Ale brakuje testów:
- ❌ Moved-from handle destruktor NIE zwalnia (implicit z MoveSourceBecomesInvalid, ale nie explicit)
- ❌ Self-move-assignment (`h = std::move(h)`) — sprawdzenie `if (this != &other)` guard
- ❌ Release happens **exactly once** — nie zero, nie dwa razy

- **Propozycja zmian kodu:**
  - Udokumentować:
    ```cpp
    // RegistrationHandle has unique ownership over an active token.
    // Moved-from handle is invalid and does not release on destruction.
    // Release is guaranteed to happen exactly once for valid handles.
    ```

- **Propozycja testów:**
  - `RegistrationHandleTest::MovedFromHandleDestructorDoesNotRelease` — explicit test z counter
  - `RegistrationHandleTest::SelfMoveAssignmentIsSafe`
  - `RegistrationHandleTest::ReleaseHappensExactlyOnce` — acquire 1 token, destroy handle, verify reuse count = 1

---

### 15. [MEDIUM] Brak testów invariantów `TokenRegistry` przy reuse tokenów

**Plik:** `include/publisher/runtime/token_registry.hpp`

**Problem:** Registry używa free listy → token reuse (`ReleaseAndReacquireReusesSameToken` test istnieje).
Ale brakuje testów kluczowych invariantów:
- ❌ Reused token dostaje **nowy** channel (nie stary)
- ❌ Released token jest naprawdę invalid (resolve powinien assertować)
- ❌ Wielokrotne cykle acquire→release→acquire zachowują spójność

Obecny test `ReleaseAndReacquireReusesSameToken` sprawdza tylko, że `token.value` się powtarza,
ale NIE weryfikuje, że nowy binding (channel) jest poprawny:

```cpp
// publisher_runtime_test.cpp:44-52 — brak sprawdzenia nowego channelu:
auto tok = reg.acquire(OutputChannel::Channel1);
reg.release(tok);
auto tok2 = reg.acquire(OutputChannel::Channel1);  // ← ten sam channel!
EXPECT_EQ(tok2.value, original_value);              // ← sprawdza value, nie channel
```

- **Propozycja zmian kodu:**
  - Dopisać invariant:
    ```cpp
    // Released token is immediately invalid.
    // Reused token MUST establish a fresh binding (channel may differ).
    ```

- **Propozycja testów:**
  - `TokenRegistryTest::ReusedTokenGetsNewChannel` — acquire(Ch0), release, acquire(Ch2) → resolve = Ch2
  - `TokenRegistryTest::ReleasedTokenIsInvalid` — death test: resolve released token → assert fires
  - `TokenRegistryTest::RepeatedAcquireReleaseCyclesStable` — 1000 cykli, verify invariants po każdym

---

### 16. [MEDIUM] `TextSink::format_impl()` — wiele heap allocations (classic layer)

**Plik:** `src/publisher/sink_publisher.cpp:17-83`

**Problem:** `format_impl` wykonuje wiele heap allocations:
- Linia 20: `result.reserve(line.size() + 100)` — `std::string` alloc
- Linia 35: `std::stoull(std::string(ts_str))` — temporary `std::string`
- Linia 52: `std::stoi(std::string(id_str))` — temporary `std::string`
- Linia 71: `std::stoi(std::string(id_str))` — temporary `std::string`
- Linia 94: `std::ostringstream oss` — heavy allocation w `format_timestamp_us()`

To jest w classic layer (nie na aktywnym hot path), ale gdyby ktoś chciał go użyć — narusza constraint.

**Wpływ na system:** Classic layer, **nieużywany w produkcji**. Najniższy priorytet w MEDIUM.

- **Propozycja zmian kodu:**
  - `std::from_chars()` zamiast `std::stoi/stoull` — zero allocations
  - `std::format()` (C++20) lub `snprintf` zamiast `std::ostringstream` w `format_timestamp_us()`
  - Pre-allocated bufor zamiast `std::string result`

- **Propozycja testów:**
  - Istniejące testy pokrywają poprawność formatowania
  - Dodatkowy test: benchmark allocation count (opcjonalnie, trudne w unit test)

---

### 17. [LOW] `publish_view()` przyjmuje `TokenRegistry&` jako non-const, ale wywołuje tylko `const` method

**Plik:** `include/publisher/runtime/publisher_runtime.hpp` — wszystkie specjalizacje

**Problem:** `publish_view()` bierze `TokenRegistry&` (mutable ref), ale wywołuje wyłącznie
`registry.resolve(token)` — który jest `const noexcept`.
Na hot path przekazywanie mutable reference gdy wystarczy const jest niepoprawne semantycznie
i uniemożliwia przekazywanie const registry.

```cpp
// Obecne:
static void publish_view(TokenRegistry& registry, ...)  // ← mutable
// Powinno być:
static void publish_view(const TokenRegistry& registry, ...)  // ← const
```

- **Propozycja zmian kodu:**
  - Zmienić `TokenRegistry&` → `const TokenRegistry&` we wszystkich 3 specjalizacjach + template `publish()`
  - 6 sygnatur do zmiany (3 × `publish_view` + 3 × `publish`)

- **Propozycja testów:**
  - Compile-time: `static_assert` że `publish_view` akceptuje `const TokenRegistry&`
  - Istniejące testy powinny przejść bez zmian

---

### 18. [LOW] `kInvalidToken` = `UINT32_MAX` — potencjalny alias z valid token

**Plik:** `include/publisher/core/publish_token.hpp:16-18`

**Problem:** `kInvalidToken` ma wartość `static_cast<uint32_t>(-1)` = `4294967295`.
`TokenRegistry::acquire()` inkrementuje `nextToken_` od 0 w górę.
Przy `kMaxBindings = 256` realnie nie dojdzie do kolizji,
ale brakuje `static_assert` gwarantującego to:

```cpp
static_assert(kMaxBindings < kInvalidToken.value,
              "kMaxBindings must be less than kInvalidToken sentinel");
```

- **Propozycja zmian kodu:**
  - Dodać powyższy `static_assert` w `token_registry.hpp`

- **Propozycja testów:**
  - Nie potrzeba — compile-time check

---

### 19. [LOW] `PublisherRuntime` — 3 specjalizacje to copy-paste

**Plik:** `include/publisher/runtime/publisher_runtime.hpp`

**Problem:** Specjalizacje `Terminal`, `File`, `Socket` różnią się wyłącznie
typem dostępu do `store` (`store.terminals[idx]`, `store.files[idx]`, `store.sockets[idx]`).
Reszta kodu jest identyczna. Narusza to DRY i zwiększa ryzyko drift'u przy zmianach.

- **Propozycja zmian kodu:**
  - Dodać do `OutputResourceStore` accessor:
    ```cpp
    template<SinkKind Kind>
    auto& get(std::size_t idx) noexcept;
    ```
  - Zastąpić 3 specjalizacje jednym generycznym szablonem
  - Mniejsza inwazyjność: zostawić specjalizacje, ale wydzielić wspólną logikę do private helper

- **Propozycja testów:**
  - Istniejące testy powinny przejść bez zmian po refaktorze
  - Dodać test `PublisherRuntimeSocketTest::PublishViewCallsWrite` (pokrycie nowej specjalizacji)

---

### 20. [LOW] Token pełni wiele ról — brak klarownej semantyki

**Pliki:** `include/publisher/core/publish_token.hpp`, `include/publisher/runtime/token_registry.hpp`

**Problem:** `PublishToken` jednocześnie:
- identyfikuje binding (obiekt → channel)
- jest kluczem do resolve channel w hot path
- reprezentuje lifecycle obiektu (acquire/release)

To zaciera semantykę systemu. Token jest "all-in-one" z nieprecyzyjną nazwą.

- **Propozycja zmian kodu:**
  - Udokumentować:
    ```cpp
    // PublishToken represents an active binding between a publisher object
    // and an OutputChannel. It is acquired from TokenRegistry and must be
    // released when the publisher is destroyed (via RegistrationHandle RAII).
    ```
  - Rozważyć zmianę nazwy: `PublishToken` → `BindingToken`
  - (Future): rozdzielić `binding_id` i `channel_id` jeśli semantyka się rozejdzie

- **Propozycja testów:**
  - Brak zmian wymaganych — to jest dokumentacja i naming

---

## LogEngine — lista problemów

---

### 21. [CRITICAL] Use-after-free przy restarcie — `init_pool_and_queue()` zwalnia stary pool, freelist trzyma dangling pointers

**Pliki:** `src/logger/core/log_engine.cpp:47-64`

**Problem:** `ensure_running()` wywołuje `init_pool_and_queue()` przy KAŻDYM restarcie (po shutdown).
`init_pool_and_queue()` robi `pool_storage_ = std::make_unique<LogRecord[]>(pool_size_)` — co **zwalnia stary pool**.
Ale `FreeList` nadal trzyma wskaźniki do rekordów ze starego pool'a (zwrócone podczas drain loop'a).
Nowe rekordy są push'owane na wierzch stosu. Stare (dangling) siedzą głębiej.

```cpp
// log_engine.cpp:57-64
void LogEngine::init_pool_and_queue()
{
    pool_size_    = 1024;
    pool_storage_ = std::make_unique<LogRecord[]>(pool_size_);  // ← FREES old pool!
    // FreeList still has pointers to old pool records (dangling!)

    for (std::size_t i = 0; i < pool_size_; ++i)
        freelist_.push(&pool_storage_[i]);  // ← pushes on TOP of dangling pointers
}
```

**Scenariusz:**
1. `ensure_running()` → `init_pool_and_queue()` (pool A, 1024 records → freelist)
2. Enqueue/process/recycle — rekordy wracają do freelist
3. `shutdown()` → drain loop zwraca wszystkie do freelist
4. `enqueue()` → `ensure_running()` CAS succeeds → `init_pool_and_queue()` → pool A **freed**
5. Freelist: [1024 new records z pool B] + [1024 dangling z pool A]
6. Przy wyczerpaniu pool B → `try_pop()` zwraca dangling pointer → **use-after-free**

**Testy:** `VerifiableEngine` i `StressableLogEngine` NIE reprodukują tego buga — inicjalizują pool
w konstruktorze, nie w `start()`. To jest **dywergencja test harness vs produkcja**.

- **Propozycja zmian kodu:**
  - Opcja A (minimalna): W `init_pool_and_queue()` przed alokacją nowego pool'a, drain'ować freelist:
    ```cpp
    void init_pool_and_queue()
    {
        // Drain stale pointers from previous pool
        while (freelist_.try_pop()) {}

        pool_size_ = 1024;
        pool_storage_ = std::make_unique<LogRecord[]>(pool_size_);
        for (std::size_t i = 0; i < pool_size_; ++i)
            freelist_.push(&pool_storage_[i]);
    }
    ```
  - Opcja B (lepsza): Dodać stan `stopped_` który UNIEMOŻLIWIA restart (patrz punkt 23)
  - Opcja C: Przenieść inicjalizację pool'a do konstruktora (jak VerifiableEngine)

- **Propozycja testów:**
  - Test `LogEngineRestart::FreesOldPoolAndDrainsFreelist`
  - Test `LogEngineRestart::NoUseAfterFreeOnSecondRun`
  - Ujednolicić VerifiableEngine i StressableLogEngine z produkcyjnym LogEngine (init w ensure_running vs konstruktor)

---

### 22. [HIGH] `FixedStringBuf` — virtual dispatch na hot path (worker loop)

**Plik:** `include/logger/core/stream_adapter.hpp:46-64`, `include/logger/core/log_engine.hpp:71-78`

**Problem:** `FixedStringBuf<N>` dziedziczy po `std::streambuf` — typie z virtual methods.
W `submit_impl` (hot path, worker loop) każde wywołanie `envelope.debug_print(os)` przechodzi
przez wirtualny dispatch:

```
os << field
  → std::ostream::write()
    → rdbuf()->sputn()        // virtual call → std::streambuf::xsputn
      → sputc() per char
        → overflow()           // virtual call → FixedStringBuf::overflow (when buffer full)
```

CLAUDE.md mówi:
- "Handler execution loop" = hot path
- "virtual functions" = forbidden constructs
- "No virtual dispatch in performance-critical paths"

Worker loop jest handler execution loop'em. `FixedStringBuf` narusza te constrainty.

```cpp
// log_engine.hpp:71-78 (worker thread, submit_impl):
auto adapter = [](const Envelope& envelope) -> std::string_view {
    thread_local FixedStringBuf<1024> buf;   // ← inherits std::streambuf (virtual)
    thread_local std::ostream os(&buf);       // ← uses virtual dispatch
    buf.reset();
    os.clear();
    envelope.debug_print(os);                 // ← every write = virtual call
    return buf.view();
};
```

Uwaga: `thread_local` amortyzuje koszt konstrukcji, ale **virtual dispatch** zachodzi na **każdym** write.

- **Propozycja zmian kodu:**
  - Opcja A (C++20, preferowana): Zastąpić `std::ostream` + `FixedStringBuf` formatowaniem do raw char bufora
    używając `std::format_to()` / `std::format_to_n()`. Wymaga przerobienia `debug_print()` z `ostream&` na inny interface
  - Opcja B (pragmatyczna): Zaakceptować virtual dispatch tutaj jako koszt integracji ze `std::ostream`.
    `FixedStringBuf` jest `thread_local`, więc koszt jest 1 vtable lookup per write (branch prediction amortyzuje).
    Udokumentować jako accepted trade-off

- **Propozycja testów:**
  - Benchmark: porównanie `FixedStringBuf` vs raw `memcpy` do bufora
  - Istniejące testy `stream_adapter_test.cpp` pokrywają poprawność

---

### 23. [HIGH] Shutdown-then-enqueue race — brak stanu "stopped permanently"

**Pliki:** `src/logger/core/log_engine.cpp:47-55, 122-130`

**Problem:** `ensure_running()` i `stop_worker()` operują na `run_` (atomic bool),
ale `run_` ma tylko dwa stany: `false` (stopped) i `true` (running).
Po `shutdown()` → `run_` = false → kolejne `enqueue()` → `ensure_running()` CAS(false→true) succeeds
→ nowy worker thread startuje → `init_pool_and_queue()` → use-after-free (punkt 21).

```cpp
// Thread A:
engine.shutdown();          // run_ = false, worker joined

// Thread B (late enqueue):
engine.enqueue(...);        // ensure_running() → CAS(false, true) succeeds!
                            // → init_pool_and_queue() → new pool, old dangling in freelist
                            // → new worker thread started
```

Nawet bez bugu z punktu 21, sam restart po shutdown jest nieokreślonym zachowaniem
— żaden test tego nie weryfikuje, dokumentacja nie istnieje.

- **Propozycja zmian kodu:**
  - Dodać trzeci stan: `enum class EngineState : uint8_t { Idle, Running, Stopped };`
  - `ensure_running()`: CAS `Idle → Running` (success), `Stopped → Stopped` (reject)
  - `stop_worker()`: CAS `Running → Stopped`
  - `enqueue()` po shutdown po prostu drop'uje z inkrementacją `dropped_`
  - Alternatywnie: `std::atomic<uint8_t>` z bit flags (running | stopped)

- **Propozycja testów:**
  - Test `LogEngineShutdown::EnqueueAfterShutdownDropsGracefully`
  - Test `LogEngineShutdown::EnsureRunningAfterShutdownReturnsFalse`
  - Stress test: concurrent shutdown + enqueue

---

### 24. [HIGH] `StressableLogEngine` nie wywołuje `submit_fn` — dywergencja test harness vs produkcja

**Plik:** `test/stress/log_engine_stress.hpp:110`

**Problem:** `StressableLogEngine::worker_loop()` wywołuje TYLKO `destroy_fn`, NIE `submit_fn`.
Porównaj z produkcyjnym `LogEngine::worker_loop()` (log_engine.cpp:96-97) który wywołuje oba:

```cpp
// Produkcja (log_engine.cpp:96-97):
rec->submit_fn(rec->storage_ptr());   // ← publishing path
rec->destroy_fn(rec->storage_ptr());  // ← cleanup

// StressableLogEngine (log_engine_stress.hpp:110):
rec->destroy_fn(rec->storage_ptr());  // ← ONLY cleanup, no submit!
```

Stress testy nie ćwiczą ścieżki submit/publish pod obciążeniem.
Bugi w `submit_impl` (np. data races, memory corruption) nie zostaną wykryte.

- **Propozycja zmian kodu:**
  - Dodać `rec->submit_fn(rec->storage_ptr());` w `StressableLogEngine::worker_loop()` przed `destroy_fn`
  - Analogicznie w drain loop'ie (linia 123)

- **Propozycja testów:**
  - Istniejące stress testy powinny nadal przechodzić (submit_fn = no-op lambda)
  - Dodać wariant z non-trivial submit_fn (np. atomic counter) aby zweryfikować poprawność

---

### 25. [HIGH] Singleton: brak jawnego `shutdown()` — ryzyko global destructor order

**Pliki:** `src/logger/core/log_engine.cpp:36-40, 42-45`, `include/logger/core/log_engine.hpp:93`

**Problem:** `LogEngine` jest singletonem (magic static). Destruktor `~LogEngine()` woła `stop_worker()`.
Ale:
1. `main.cpp` NIE woła `LogEngine::instance().shutdown()` — polegamy na destruktorze
2. Destruktory magic statics uruchamiają się w odwrotnej kolejności konstrukcji
3. Jeśli inny globalny obiekt loguje w swoim destruktorze PO destrukcji LogEngine → UB
4. `std::cout` (używany przez `OutputResourceStore::store()`) może być zniszczony przed LogEngine

```cpp
// log_engine.cpp:17-26 — store() trzyma wskaźnik do std::cout:
s.terminals[...].out = &std::cout;
// Jeśli std::cout jest zniszczony przed LogEngine, worker thread pisze do freed memory
```

- **Propozycja zmian kodu:**
  - Opcja A (minimalna): Dodać `std::atexit` callback w `LogEngine::instance()`:
    ```cpp
    static LogEngine& instance() noexcept {
        static LogEngine eng;
        static bool registered = (std::atexit([] { eng.shutdown(); }), true);
        return eng;
    }
    ```
  - Opcja B (explicit): RAII guard w `main()`:
    ```cpp
    int main() {
        // ... setup ...
        auto& engine = LogEngine::instance();
        // ... use ...
        engine.shutdown();  // explicit, before main returns
    }
    ```
  - Opcja C: `[[gnu::init_priority]]` / constructor attribute aby kontrolować kolejność

- **Propozycja testów:**
  - Trudne do przetestowania w unit teście (global lifetime)
  - Test integracyjny: program z globalnym loggerem + shutdown guard, valgrind clean

---

### 26. [MEDIUM] Hidden globals: `registry()` i `store()` to magic statics wewnątrz LogEngine

**Plik:** `src/logger/core/log_engine.cpp:9-29`

**Problem:** `TokenRegistry` i `OutputResourceStore` to `static local` variables w metodach klasy,
a nie member variables. To tworzy:

1. **Implicit coupling:** LogEngine, Publisher i singleton mają ukrytą zależność
2. **Initialization order:** `publishHandle_` w konstruktorze LogEngine woła `registry()` —
   jeśli `registry().acquire()` rzuci wyjątek (capacity exceeded), singleton jest częściowo skonstruowany
3. **Testing impossible:** Nie da się podmienić registry/store w testach — stąd osobne harnessy
   (VerifiableEngine, StressableLogEngine, TestLogEngine)
4. **Dual path w main.cpp:** `main.cpp` tworzy WŁASNY `TokenRegistry` i `OutputResourceStore`
   (linia 20-23), kompletnie niezależny od tych w LogEngine. Dwa systemy publisher'owe.

```cpp
// log_engine.cpp:9-13 — hidden static:
publisher::runtime::TokenRegistry& LogEngine::registry() noexcept {
    static publisher::runtime::TokenRegistry registry_;  // ← hidden global
    return registry_;
}

// main.cpp:20 — separate instance:
TokenRegistry registry;  // ← completely separate from LogEngine's registry!
```

- **Propozycja zmian kodu:**
  - Opcja A (dependency injection): Przekazać `TokenRegistry*` i `OutputResourceStore*` jako parametry
    konstruktora. Singleton `instance()` może tworzyć domyślne, ale testy mogą podać własne
  - Opcja B (minimal): Udokumentować że `main.cpp` to demo i produkcyjnie ma się używać LogEngine::instance()
  - Opcja C (refactor): Przenieść `registry_` i `store_` do member variables LogEngine

- **Propozycja testów:**
  - Jeśli DI (opcja A): Test `LogEngineTest::CustomRegistryAndStore`
  - Test integracyjny: `LogEngine::instance().enqueue()` → weryfikacja output na `std::cout`

---

### 27. [MEDIUM] `submit_impl` — rekurencyjny dostęp do singletona z worker thread

**Plik:** `include/logger/core/log_engine.hpp:83-88`

**Problem:** `submit_impl` jest wywoływany z worker thread'a. Wewnątrz woła `instance()`:

```cpp
publisher::runtime::PublisherRuntime<...>::publish_view(
    registry(),
    store(),
    instance().publishHandle_.token(),  // ← recursive singleton access
    view
);
```

`instance()` jest bezpieczne (magic static, already constructed), ale:
1. Tight coupling: submit_impl musi znać singleton
2. Nie da się testować submit_impl w izolacji
3. Każdy call to `instance()` to load static guard + branch (koszt marginalny, ale niepotrzebny)

- **Propozycja zmian kodu:**
  - Przekazać `PublishToken` jako parametr do `submit_fn` (zmiana sygnatury `SubmitFn`):
    ```cpp
    using SubmitFn = void (*)(void* storage, PublishToken token);
    ```
  - Lub: cache'ować token w `LogRecord` (dodać pole `PublishToken token_`)
  - Lub: zaakceptować jako pragmatyczne rozwiązanie (singleton jest tani)

- **Propozycja testów:**
  - Jeśli zmiana sygnatury: zaktualizować VerifiableEngine i StressableLogEngine
  - Test: mock publish_view weryfikujący poprawny token

---

### 28. [MEDIUM] Pool size hardcoded na 1024

**Plik:** `src/logger/core/log_engine.cpp:59`

**Problem:** `pool_size_ = 1024` jest hardcoded. Nie da się go skonfigurować.
- Na Raspberry Pi (target per comment w lockfree_queue.hpp:10): 1024 × ~384B = ~384KB RAM — dużo
- Na serwerze high-throughput: 1024 rekordów może być za mało (dropped > 0 pod obciążeniem)

```cpp
void LogEngine::init_pool_and_queue()
{
    pool_size_ = 1024;  // ← hardcoded
    pool_storage_ = std::make_unique<LogRecord[]>(pool_size_);
    ...
}
```

- **Propozycja zmian kodu:**
  - `static constexpr std::size_t kDefaultPoolSize = 1024;`
  - Opcja konfiguracji przed pierwszym `enqueue()` (np. `LogEngine::configure(pool_size)`)
  - Lub: template parameter na pool size (compile-time)

- **Propozycja testów:**
  - Test `LogEngineConfig::CustomPoolSize`
  - Istniejące stress testy już parametryzują pool size w VerifiableEngine/StressableLogEngine

---

### 29. [LOW] `MpscQueue::pop()` — busy-wait spin loop, nie jest w pełni lock-free

**Plik:** `include/logger/core/lockfree_queue.hpp:83-87`

**Problem:** Gdy producer wykonał `tail_.exchange(n)` ale jeszcze nie zrobił `prev->next.store(n)`,
consumer w `pop()` wchodzi w spin-wait:

```cpp
// lockfree_queue.hpp:83-87:
do {
    next = head->next.load(std::memory_order_acquire);
} while (!next);  // ← spin-wait for producer's next pointer
```

Technicznie to nie jest lock-free (progress guarantee wymaga, żeby przynajmniej jeden thread
robił postęp — tu consumer jest zablokowany przez producer'a).
W praktyce okienko jest mikrosekundowe (producer robi jeden `store` po `exchange`).

- **Propozycja zmian kodu:**
  - Zaakceptować jako known trade-off (MPSC queue design choice)
  - Dodać komentarz dokumentujący: `// Bounded spin: producer will complete next.store within ~ns`
  - Opcjonalnie: dodać `yield()` po N iteracji aby nie spalić CPU

- **Propozycja testów:**
  - Istniejące stress testy pokrywają ten scenariusz pod obciążeniem
  - Dodać test `MpscQueueTest::PopReturnsAfterDelayedNextPointer` (symulacja opóźnionego store'a)

---

### 30. [LOW] `FixedStringBuf::overflow()` — silent truncation bez diagnostyki

**Plik:** `include/logger/core/stream_adapter.hpp:58-61`

**Problem:** Gdy bufor (1024B) się zapełni, `overflow()` po cichu zjada kolejne bajty:

```cpp
int_type overflow(int_type ch) override
{
    return ch;  // consume but discard — no diagnostic
}
```

Jeśli log message > 1024B, jest ucięty bez śladu. Caller nie wie, że stracił dane.

- **Propozycja zmian kodu:**
  - Dodać flagę `bool truncated_{false}` i ustawić ją w `overflow()`
  - Dodać getter `[[nodiscard]] bool truncated() const noexcept`
  - W `submit_impl` po `debug_print()` sprawdzić flagę i np. dopisać `[TRUNCATED]` do view

- **Propozycja testów:**
  - Test `FixedStringBufTest::OverflowSetsTruncatedFlag`
  - Test `FixedStringBufTest::ResetClearsTruncatedFlag`
  - Test `FixedStringBufTest::MessageExactly1024BytesIsNotTruncated`

---

### 31. [LOW] Singleton uniemożliwia testowanie — 3 osobne harnessy zamiast jednej testowalnej klasy

**Pliki:** `test/logger/core/verifiable_engine.hpp`, `test/stress/log_engine_stress.hpp`, `test/logger/mocks/mock_log_engine.hpp`

**Problem:** Singleton `LogEngine` jest nietestowalny bezpośrednio (shared state, brak reset, brak DI).
W rezultacie istnieją 3 osobne implementacje engine'a do testów:

1. **`VerifiableEngine`** — pipeline testy (submit_fn counter)
2. **`StressableLogEngine`** — stress testy (brak submit_fn! → punkt 24)
3. **`TestLogEngine`** — mock z `std::ostringstream` (heap alloc, classic Publisher path)

Każdy harness powiela worker_loop ręcznie, z subtelnymi różnicami:
- `StressableLogEngine` nie wywołuje `submit_fn` (punkt 24)
- `VerifiableEngine` init w konstruktorze, LogEngine init w `ensure_running()` (punkt 21)
- `TestLogEngine` nie ma worker thread'a w ogóle — synchroniczny

Ta trzykrotna duplikacja to breeding ground dla bugów dywergencji.

- **Propozycja zmian kodu:**
  - Wydzielić wspólny `EngineCore` (non-singleton) z pełną logiką: pool, queue, worker, submit/destroy
  - `LogEngine` = singleton wrapper wokół `EngineCore`
  - Testy operują bezpośrednio na `EngineCore` bez duplikacji
  - Alternatywnie: template parameter na LogEngine (tag dispatch singleton vs instancjowalny)

- **Propozycja testów:**
  - Zunifikować VerifiableEngine i StressableLogEngine do jednego harnessu
  - Dodać test `LogEngineCore::EndToEndWithPublisher` — pełen pipeline z prawdziwym submit_impl

---

## Podsumowanie priorytetów

### Publisher (20 punktów) — posortowane wg wpływu na system w ramach severity

| # | Severity | Problem | Hot path? | Ma testy? |
|---|----------|---------|-----------|-----------|
| 1 | CRITICAL | Assert-only null guard → UB w Release | TAK | Brak edge case |
| 2 | CRITICAL | Assert-only token validation → UB w Release | TAK | Brak edge case |
| 3 | CRITICAL | toIndex() brak bounds check → OOB | TAK | Brak |
| 4 | HIGH | Brak exclusive channel ownership | TAK (runtime) | Brak |
| 5 | HIGH | Brak modelu concurrency publish path | TAK (runtime) | Brak |
| 6 | HIGH | Channel ≠ Resource — brak modelu | TAK (runtime) | Brak |
| 7 | HIGH | Brak static_assert(!is_polymorphic_v) | — | N/A (compile-time) |
| 8 | HIGH | FilePolicy missing rule-of-five | NIE (classic) | Brak |
| 9 | HIGH | PolicyBase::publish() heap alloc | NIE (classic) | Jest happy path |
| 10 | MEDIUM | Brak lifetime contract store vs publish | TAK (runtime) | Brak |
| 11 | MEDIUM | OutputResourceStore brak validate() | TAK (runtime) | Brak |
| 12 | MEDIUM | RegistrationHandle silent exception swallowing | NIE | Partial |
| 13 | MEDIUM | Socket sink stub — dane znikają | TAK | Brak |
| 14 | MEDIUM | RegistrationHandle — luki w testach ownership | NIE | Częściowo (3/6) |
| 15 | MEDIUM | Token reuse — brak testów invariantów | NIE | Częściowo (1/3) |
| 16 | MEDIUM | TextSink heap allocations | NIE (classic) | Jest happy path |
| 17 | LOW | publish_view non-const registry ref | TAK | N/A |
| 18 | LOW | kInvalidToken potential alias | — | N/A (compile-time) |
| 19 | LOW | PublisherRuntime copy-paste | — | Jest |
| 20 | LOW | Token — brak klarownej semantyki | — | N/A (docs) |

### LogEngine (11 punktów)

| # | Severity | Problem | Hot path? | Ma testy? |
|---|----------|---------|-----------|-----------|
| 21 | CRITICAL | Use-after-free przy restarcie (pool freed, freelist dangling) | TAK | **Brak** — harnessy nie reprodukują |
| 22 | HIGH | FixedStringBuf virtual dispatch na hot path | TAK (worker) | Jest poprawność, brak perf |
| 23 | HIGH | Shutdown-then-enqueue race (brak stanu "stopped") | TAK | Brak |
| 24 | HIGH | StressableLogEngine nie wywołuje submit_fn | — (test) | Dywergencja test/prod |
| 25 | HIGH | Brak jawnego shutdown() → global destructor order UB | NIE | Brak |
| 26 | MEDIUM | Hidden globals (registry/store magic statics) | NIE | N/A (design) |
| 27 | MEDIUM | submit_impl recursive singleton access | TAK (worker) | Brak izolowany |
| 28 | MEDIUM | Pool size hardcoded (1024) | NIE | N/A (config) |
| 29 | LOW | MpscQueue::pop() spin-wait (nie lock-free) | TAK | Jest (stress) |
| 30 | LOW | FixedStringBuf silent truncation | TAK (worker) | Brak |
| 31 | LOW | 3 osobne harnessy z dywergencją | — (test) | N/A (design) |

---

## Brakujące obszary testowe — podsumowanie

| Obszar | Istniejące pokrycie | Brakuje |
|--------|-------------------|---------|
| Ownership semantics RegistrationHandle | 3 testy (move ctor, move assign, destructor) | moved-from destructor, self-move, exactly-once |
| Exclusive channel ownership | Brak | acquire same channel twice = error |
| Token reuse invariants | 1 test (value reuse) | channel rebinding, released = invalid, cycle stability |
| Concurrency contract publish_view() | Brak | dokumentacja + opcjonalnie thread affinity assert |
| Channel vs resource semantics | Brak | 1:1 enforcement lub shared model |
| Lifetime contract OutputResourceStore | Brak | store outlives publish, immutable after init |
| LogEngine restart (shutdown → enqueue) | Brak | use-after-free, state machine |
| LogEngine singleton end-to-end | Brak (proxy harnessy) | instance().enqueue() → output verification |
| submit_fn under stress | Brak (StressableLogEngine skip) | stress z non-trivial submit_fn |

---

## Kluczowe wnioski architektoniczne

**Singleton nie jest problemem sam w sobie** — magic static jest thread-safe (C++11 guarantee),
inicjalizacja jest poprawna, worker lifecycle jest dobrze zsynchronizowany.

**Problemy wynikające z singletona:**
- Brak testowalności → 3 harnessy → dywergencja → bug #21 nie wykryty w testach
- Brak DI → hidden globals → tight coupling z publisher
- Brak kontroli lifecycle → restart = use-after-free, no permanent stop

**Brakujące decyzje architektoniczne (wymagają podjęcia):**
- Model concurrency dla publish path (single-thread vs per-channel multi-thread)
- Model ownership channelu (exclusive vs shared resource)
- Model lifetime store'a (immutable after init vs runtime rebinding)

**Co działa dobrze:**
- MPSC queue: poprawne memory ordering, lock-free push, correct sentinel node design
- Pool/freelist: zero-alloc hot path, cache-line aligned records, graceful backpressure (drops)
- Atomics: properly ordered (acq_rel na control flow, relaxed na counters)
- Drain loop: poprawny (submit_fn + destroy_fn), queue reset po drain
- Type erasure: raw function pointers (no std::function), compile-time size checks

---

## Build Errors (if any)

*To be populated after build attempt.*

---

## Test Results (if any)

*To be populated after test run.*
