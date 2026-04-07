# Plan wdrożenia poprawek Publisher + LogEngine

**Date:** 2026-04-05
**Źródło:** `claude/proposal_improvements_publisher.md` (dokument audytowy)

---

## Zasada

Dokument audytowy (`proposal_improvements_publisher.md`) jest posortowany wg **severity** — to dobra kolejność do analizy, ale nie do wdrażania.

Ten dokument definiuje **kolejność implementacji**:

1. **Najpierw kontrakt architektoniczny** — bez niego dalsze punkty wiszą w próżni
2. **Potem invariants i safety** — zamknięcie UB/OOB
3. **Dopiero potem ownership model** — wynika z decyzji z fazy 1
4. **Na końcu cleanup i refaktor** — niska wartość, zero ryzyka

LogEngine ma własną fazę — jego problemy są w dużej mierze ortogonalne do publishera.

---

## Faza 1: Kontrakt architektoniczny

**Cel:** Podjąć decyzje, które warunkują wszystkie dalsze zmiany.
**Output:** Komentarze kontraktowe w kodzie, zero zmian w logice.

| Krok | Audyt # | Decyzja do podjęcia | Pliki |
|------|---------|---------------------|-------|
| 1.1 | #5 | Model concurrency `publish_view()`: single-thread only (worker) vs multi-thread per channel | `publisher_runtime.hpp` |
| 1.2 | #6 | Channel ≠ Resource: exclusive 1:1 vs shared z synchronizacją | `resource_store.hpp`, `log_engine.cpp` |
| 1.3 | #10 | Lifetime `OutputResourceStore`: immutable after init vs runtime rebinding | `resource_store.hpp`, `publisher_runtime.hpp` |
| 1.4 | #23 | LogEngine state machine: `Idle → Running → Stopped` (brak restartu) | `log_engine.hpp`, `log_engine.cpp` |

### 1.1 Model concurrency dla publish path (audyt #5)

**Decyzja:** Czy `publish_view()` jest wywoływane:
- **(A)** wyłącznie z jednego worker thread'a (obecny stan de facto),
- **(B)** z wielu wątków, ale na rozłącznych channelach.

**Wpływ na dalsze fazy:**
- Opcja A → nie potrzeba synchronizacji w SinkTraits, store, handle'ach
- Opcja B → wymagana gwarancja rozłączności + testy stress multi-thread

**Implementacja (po decyzji):**
```cpp
// publisher_runtime.hpp — komentarz kontraktowy:
// Contract: publish_view() MUST be called from a single consumer thread.
// Concurrent calls from multiple threads are undefined behavior.
// Rationale: LogEngine dispatches via single worker thread (MPSC consumer).
```

Opcjonalnie (Debug only):
```cpp
#ifndef NDEBUG
static std::thread::id owner_thread_{};
static std::once_flag flag_;
std::call_once(flag_, [&]{ owner_thread_ = std::this_thread::get_id(); });
assert(std::this_thread::get_id() == owner_thread_ && "Thread affinity violated");
#endif
```

---

### 1.2 Channel ≠ Resource — model exclusive vs shared (audyt #6)

**Decyzja:** Czy wiele channeli może wskazywać ten sam zasób (np. `&std::cout`)?

**Stan obecny:** `LogEngine::store()` przypisuje `&std::cout` do **4 channeli** (log_engine.cpp:20-23).
To de facto shared resource model — ale bez dokumentacji i synchronizacji.

**Opcje:**
- **(A) Exclusive 1:1** — channel = unikatowy resource. Wymaga przerobienia `LogEngine::store()` (np. 1 channel = stdout, reszta = file/null)
- **(B) Shared allowed** — udokumentować, że shared jest OK bo single-thread consumer (wynika z 1.1A)
- **(C) Shared z guardem** — validate() wykrywa shared resources i wymaga explicit opt-in

**Implementacja (po decyzji):**
```cpp
// resource_store.hpp — komentarz kontraktowy:
// Default invariant:
// Each OutputChannel SHOULD own a unique resource.
// Shared resources (same pointer across channels) are permitted ONLY when
// publish_view() is called from a single consumer thread (see Contract in publisher_runtime.hpp).
```

---

### 1.3 Lifetime contract OutputResourceStore (audyt #10)

**Decyzja:** Czy store jest immutable po inicjalizacji, czy dozwolony jest runtime rebinding?

**Stan obecny:** Store jest `static local` w LogEngine — żyje do końca programu, nigdy się nie zmienia.
Ale nic tego nie wymusza dla callsitów spoza LogEngine (np. `main.cpp` tworzy own store).

**Implementacja (po decyzji):**
```cpp
// resource_store.hpp:
// Contract: OutputResourceStore MUST outlive all publish operations.
// Handles MUST NOT be modified after the first call to publish_view().
// Rationale: publish_view() reads handles without synchronization.
```

---

### 1.4 LogEngine state machine — brak permanent stop (audyt #23)

**Decyzja:** Czy LogEngine po `shutdown()` może być ponownie uruchomiony?

**Problem:** Obecny `run_` (atomic bool) pozwala na restart → `ensure_running()` CAS succeeds → `init_pool_and_queue()` → use-after-free (audyt #21).

**Preferowana opcja:**
```cpp
enum class EngineState : uint8_t { Idle, Running, Stopped };
std::atomic<uint8_t> state_{static_cast<uint8_t>(EngineState::Idle)};

// ensure_running(): CAS Idle→Running (OK), Stopped→Stopped (reject, drop log)
// stop_worker():    CAS Running→Stopped
```

**Wpływ:** Zamyka audyt #21 (use-after-free) jako side effect — restart nie jest możliwy, więc stary pool nigdy nie jest zwalniany z dangling freelist.

---

## Faza 2: Invariants i safety

**Cel:** Zamknąć UB, OOB, null deref. Gwarantować poprawność na hot path.
**Zależność:** Decyzje z fazy 1 muszą być podjęte (kontrakty dodane).

| Krok | Audyt # | Co | Pliki |
|------|---------|-----|-------|
| 2.1 | #2 | Token validation — wzmocnić `resolve()` invariant | `token_registry.hpp` |
| 2.2 | #3 | `toIndex()` bounds — assert + static_assert | `publisher_types.hpp` |
| 2.3 | #1 | Null guards `SinkTraits::write()` — validate na cold path | `sink_traits.hpp`, `resource_store.hpp` |
| 2.4 | #11 | `OutputResourceStore::validate()` — cold path check | `resource_store.hpp` |
| 2.5 | #21 | Use-after-free na restart — drain freelist lub zablokować restart (wynika z 1.4) | `log_engine.cpp` |
| 2.6 | #12 | `RegistrationHandle::release()` — assert w catch block | `registration_handle.hpp` |

### 2.1 Token validation (audyt #2)

**Strategia:** Hot path zostaje szybki (assert only). Safety jest gwarantowane przez cold path invariant:
- `acquire()` zwraca valid token (zawsze)
- `RegistrationHandle` trzyma token — RAII gwarantuje lifetime
- Do `publish_view()` docierają TYLKO tokeny z `acquire()` → invariant jest naturalny

**Implementacja:**
```cpp
// token_registry.hpp — resolve() bez zmian (assert only na hot path)
// Dodać cold path check:
[[nodiscard]] bool is_active(PublishToken token) const noexcept
{
    return isValidToken(token) && tokenUsed_[token.value];
}
```

**Testy:**
- `TokenRegistryTest::ResolveWithMaxValueTokenIsHandled` (death test, Debug)
- `TokenRegistryTest::AcquireRelease256TokensAndResolve`

---

### 2.2 toIndex() bounds (audyt #3)

**Implementacja:**
```cpp
constexpr std::size_t toIndex(OutputChannel channel) noexcept {
    assert(static_cast<std::size_t>(channel) < static_cast<std::size_t>(OutputChannel::Count)
           && "Channel out of bounds");
    return static_cast<std::size_t>(channel);
}
```

Plus compile-time:
```cpp
static_assert(toIndex(OutputChannel::Channel0) == 0);
static_assert(toIndex(OutputChannel::Channel3) == 3);
```

---

### 2.3 Null guards SinkTraits (audyt #1)

**Strategia:** Nie dodawać runtime check na hot path. Zamiast tego: gwarancja na cold path via `validate()` (krok 2.4).

Assert'y w `SinkTraits::write()` zostają jako dokumentacja precondition.
Safety wynika z: store jest validate'owany po init → null handles nie docierają do hot path.

---

### 2.4 OutputResourceStore::validate() (audyt #11)

**Implementacja:**
```cpp
// resource_store.hpp:
[[nodiscard]] bool validate(SinkKind kind) const noexcept
{
    switch (kind) {
    case SinkKind::Terminal:
        for (auto& t : terminals)
            if (!t.out) return false;
        return true;
    case SinkKind::File:
        for (auto& f : files)
            if (!f.file) return false;
        return true;
    case SinkKind::Socket:
        // not implemented — always invalid
        return false;
    }
    return false;
}
```

Wywołanie w `LogEngine::ensure_running()` lub po inicjalizacji store.

**Testy:**
- `OutputResourceStoreTest::DefaultConstructedHasNullTerminals`
- `OutputResourceStoreTest::ValidateReturnsFalseForUninitialized`
- `OutputResourceStoreTest::ValidateReturnsTrueAfterProperInit`

---

### 2.5 Use-after-free na restart (audyt #21)

**Zależność od 1.4:**
- Jeśli state machine (Stopped → no restart): problem znika — `init_pool_and_queue()` nigdy nie jest wywoływany powtórnie
- Jeśli restart dozwolony: `init_pool_and_queue()` musi drain'ować freelist przed alokacją nowego pool'a:
  ```cpp
  while (freelist_.try_pop()) {}  // drain stale pointers
  ```

---

### 2.6 Silent exception swallowing (audyt #12)

**Implementacja:**
```cpp
void release() noexcept
{
    if (registry_ != nullptr && token_.value != kInvalidToken.value)
    {
        try { registry_->release(token_); }
        catch (...) {
            assert(false && "Unexpected exception in RegistrationHandle::release()");
        }
    }
    registry_ = nullptr;
    token_ = kInvalidToken;
}
```

---

## Faza 3: Ownership model

**Cel:** Wymusić exclusive ownership channelu, udokumentować semantykę tokenów, pokryć testami.
**Zależność:** Decyzje z fazy 1 (exclusive channel, concurrency model) muszą być podjęte.

| Krok | Audyt # | Co | Pliki |
|------|---------|-----|-------|
| 3.1 | #4 | Exclusive channel ownership w TokenRegistry | `token_registry.hpp` |
| 3.2 | #15 | Testy invariantów token reuse | `publisher_runtime_test.cpp` |
| 3.3 | #14 | Testy ownership RegistrationHandle (luki) | `publisher_runtime_test.cpp` |
| 3.4 | #20 | Dokumentacja semantyki tokena | `publish_token.hpp` |

### 3.1 Exclusive channel ownership (audyt #4)

**Implementacja:**
```cpp
// token_registry.hpp:
std::array<bool, kChannelCount> channelUsed_{};

[[nodiscard]] PublishToken acquire(OutputChannel channel)
{
    const auto idx = toIndex(channel);
    if (channelUsed_[idx])
        throw std::runtime_error("Channel already acquired");

    // ... existing token allocation ...
    channelUsed_[idx] = true;
    return token;
}

void release(PublishToken token)
{
    // ... existing validation ...
    const auto ch = tokenToChannel_[token.value];
    channelUsed_[toIndex(ch)] = false;
    // ... existing cleanup ...
}
```

**Testy:**
- `TokenRegistryTest::AcquireSameChannelTwiceFails`
- `RegistrationHandleTest::ChannelBecomesFreeAfterRelease`

**Uwaga:** To łamie obecny `LogEngine::store()` (4 channele → 1 resource). Wymagana zmiana w `log_engine.cpp` — ale ta decyzja wynika z fazy 1.2.

---

### 3.2 Testy token reuse (audyt #15)

```cpp
TEST(TokenRegistryTest, ReusedTokenGetsNewChannel)
{
    TokenRegistry reg;
    auto tok = reg.acquire(OutputChannel::Channel0);
    const auto val = tok.value;
    reg.release(tok);
    auto tok2 = reg.acquire(OutputChannel::Channel2);
    EXPECT_EQ(tok2.value, val);                        // reused slot
    EXPECT_EQ(reg.resolve(tok2), OutputChannel::Channel2);  // NEW channel
}

TEST(TokenRegistryTest, RepeatedAcquireReleaseCyclesStable)
{
    TokenRegistry reg;
    for (int i = 0; i < 1000; ++i) {
        auto tok = reg.acquire(OutputChannel::Channel0);
        EXPECT_EQ(reg.resolve(tok), OutputChannel::Channel0);
        reg.release(tok);
    }
}
```

---

### 3.3 Testy RegistrationHandle (audyt #14)

Istniejące:
- MoveSourceBecomesInvalid
- MoveAssignmentReleasesDestination
- TokenReleasedOnDestruction

Brakujące:
```cpp
TEST(RegistrationHandleTest, MovedFromHandleDestructorDoesNotRelease)
{
    TokenRegistry reg;
    uint32_t val;
    {
        RegistrationHandle h1(reg, OutputChannel::Channel0);
        val = h1.token().value;
        RegistrationHandle h2 = std::move(h1);
        // h1 destructor fires here — should NOT release
    }
    // h2 destructor released → token available
    RegistrationHandle h3(reg, OutputChannel::Channel0);
    EXPECT_EQ(h3.token().value, val);  // reused once, not twice
}

TEST(RegistrationHandleTest, SelfMoveAssignmentIsSafe)
{
    TokenRegistry reg;
    RegistrationHandle h(reg, OutputChannel::Channel0);
    auto val = h.token().value;
    h = std::move(h);
    EXPECT_TRUE(h.valid());
    EXPECT_EQ(h.token().value, val);
}
```

---

### 3.4 Dokumentacja semantyki tokena (audyt #20)

```cpp
// publish_token.hpp:
// PublishToken represents an active binding between a publisher object
// and an OutputChannel. It is acquired from TokenRegistry and must be
// released when the publisher is destroyed (via RegistrationHandle RAII).
//
// A token is:
//   - a unique identifier for a binding (index into TokenRegistry arrays)
//   - a key to resolve the bound OutputChannel on the hot path
//   - an RAII-managed lifecycle object (acquire/release)
```

---

## Faza 4: Cleanup i refaktor

**Cel:** Porządki. Niska wartość, zero ryzyka.
**Zależność:** Brak. Może iść równolegle z fazą 3.

| Krok | Audyt # | Co | Pliki |
|------|---------|-----|-------|
| 4.1 | #17 | `const TokenRegistry&` w publish_view | `publisher_runtime.hpp` |
| 4.2 | #19 | Eliminacja copy-paste (3 specjalizacje → 1 template) | `publisher_runtime.hpp`, `resource_store.hpp` |
| 4.3 | #7 | `static_assert(!is_polymorphic_v)` na typach publishera | wszystkie nagłówki publisher |
| 4.4 | #18 | `static_assert(kMaxBindings < kInvalidToken.value)` | `token_registry.hpp` |
| 4.5 | #8 | FilePolicy — explicit deleted copy, defaulted move | `policy_publisher.hpp` |
| 4.6 | #9 | PolicyBase::publish() — deprecation lub FixedStringBuf | `policy_publisher.hpp` |
| 4.7 | #16 | TextSink — `std::from_chars` zamiast `std::stoi` | `sink_publisher.cpp` |
| 4.8 | #13 | Socket sink — `[[deprecated]]` lub usunąć z enum | `sink_traits.hpp` |

Wszystko tutaj jest mechaniczne i bezpieczne. Kolejność dowolna.

---

## Faza 5: LogEngine — zmiany strukturalne

**Cel:** Naprawić bugi i dywergencje w LogEngine.
**Zależność:** Faza 1.4 (state machine) i faza 2.5 (UAF). Reszta niezależna.

| Krok | Audyt # | Co | Priorytet |
|------|---------|-----|-----------|
| 5.1 | #24 | StressableLogEngine: dodać `submit_fn` call | Wysoki — dywergencja test/prod |
| 5.2 | #25 | Singleton shutdown: `std::atexit` lub explicit guard w main | Wysoki — global destructor UB |
| 5.3 | #22 | FixedStringBuf virtual dispatch — decyzja: akceptacja vs `std::format_to` | Średni — trade-off |
| 5.4 | #26 | Hidden globals → dependency injection lub dokumentacja | Średni — design |
| 5.5 | #27 | submit_impl recursive singleton → cache token w LogRecord | Niski |
| 5.6 | #28 | Pool size → `constexpr kDefaultPoolSize` + configure() | Niski |
| 5.7 | #30 | FixedStringBuf truncation flag | Niski |
| 5.8 | #29 | MpscQueue spin-wait → komentarz + opcjonalny yield | Niski |
| 5.9 | #31 | 3 harnessy → zunifikowany EngineCore | Duży refaktor, na końcu |

### 5.1 StressableLogEngine submit_fn (audyt #24)

**Najszybsza poprawka:** Dodać `rec->submit_fn(rec->storage_ptr());` przed `destroy_fn` w worker_loop i drain.

```cpp
// test/stress/log_engine_stress.hpp — worker_loop:
rec->submit_fn(rec->storage_ptr());   // ← ADD THIS
rec->destroy_fn(rec->storage_ptr());
```

---

### 5.2 Singleton shutdown (audyt #25)

**Preferowana opcja:** Explicit `shutdown()` w `main()`:
```cpp
int main() {
    // ... setup ...
    auto& engine = LogEngine::instance();
    // ... use ...
    engine.shutdown();  // before main returns, before static destructors
}
```

Backup: `std::atexit` callback w `instance()`.

---

## Podsumowanie kolejności

```
Faza 1: Decyzje architektoniczne          ← WYMAGA TWOJEGO INPUTU
  1.1  Concurrency model (single-thread)
  1.2  Channel vs Resource (exclusive/shared)
  1.3  Store lifetime (immutable after init)
  1.4  LogEngine state machine (no restart)

Faza 2: Safety (UB/OOB)                   ← zależna od fazy 1
  2.1  Token validation (cold path check)
  2.2  toIndex() bounds
  2.3  Null guards (via validate)
  2.4  OutputResourceStore::validate()
  2.5  Use-after-free restart
  2.6  Exception swallowing

Faza 3: Ownership model                   ← zależna od fazy 1
  3.1  Exclusive channel ownership
  3.2  Token reuse tests
  3.3  RegistrationHandle test gaps
  3.4  Token semantics docs

Faza 4: Cleanup & refaktor               ← niezależna
  4.1-4.8  const-correctness, copy-paste, static_asserts, classic layer

Faza 5: LogEngine structural              ← częściowo zależna od fazy 1.4
  5.1-5.9  test harness fix, shutdown, FixedStringBuf, DI, pool config
```

---

## Następny krok

**Faza 1 wymaga Twoich decyzji.** Proponuję zacząć od:

1. **1.1** — Potwierdzić: `publish_view()` = single-thread only (worker). Tak/Nie?
2. **1.2** — Shared `&std::cout` across channels: akceptowalne (bo single-thread) czy wymuszać 1:1?
3. **1.3** — Store immutable after init: Tak/Nie?
4. **1.4** — LogEngine no restart (Idle→Running→Stopped): Tak/Nie?

Po decyzjach — mogę wdrażać fazę 2 i 3 od razu.

---

## Build Errors (if any)

*To be populated after build attempt.*

---

## Test Results (if any)

*To be populated after test run.*
