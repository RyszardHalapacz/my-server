# Proposal: Publisher Architecture Refactor

**Date:** 2026-03-30

---

## Original Prompt

Zaprojektuj architekturę, która rozdziela odpowiedzialności w warstwie publish:
config, registry, binding, publisher, sink, policy — z runtime reconfiguration
i zero-lookup hot path.

---

## Context

Obecny system ma jeden hardcoded publish path:

```
submit_impl → Publisher<TerminalPolicy, TextSink>::publish(env, adapter)
```

Każdy rekord — niezależnie od źródła — idzie na stdout z TextSink.

FilePolicy tworzy `std::ofstream` per publish() call (open/write/close per log).
To jest nieakceptowalne.

Problem nie leży w FilePolicy. Problem leży w braku warstwy runtime, która zarządza:
- ownership zasobów (ofstream, FILE*, socket)
- routingiem (handler → output)
- lifecycle (otwarcie/zamknięcie zasobów)
- wiązaniem handler → output (binding)

---

# ARCHITEKTURA

## 1. Model mentalny

System ma dwie osie:

**Oś compile-time**: typ envelopy, format (Sink), adapter (debug_print → string_view).
Te rzeczy są znane w momencie kompilacji i nie powinny być resolwowane w runtime.

**Oś runtime**: *dokąd* idzie output, *czy* jest aktywny, *jaki* filtr jest zastosowany.
Te rzeczy zmieniają się między uruchomieniami i potencjalnie w trakcie pracy.

Obecny system traktuje OBE osie jako compile-time. Stąd problem:
- nie można wskazać różnych outputów per handler
- nie można zmienić outputu w runtime
- FilePolicy nie ma gdzie trzymać stanu

**Kluczowy insight**: `submit_fn` w LogRecord jest już type-erased function pointer.
To jest mechanizm dispatch — wystarczy go sparametryzować o PublishId,
żeby worker thread automatycznie trafiał do odpowiedniego bindingu.

### Warstwy systemu

```
┌─────────────────────────────────────────────────────────┐
│                     COLD PATH                           │
│                                                         │
│  OutputRegistry         RuntimeConfig                   │
│  ┌──────────────┐      ┌──────────────────┐            │
│  │ owns:        │      │ per PublishId:    │            │
│  │  ofstream*   │      │  enabled?         │            │
│  │  FILE*       │      │  filter           │            │
│  │  socket      │      │  channel_id       │            │
│  └──────┬───────┘      └────────┬─────────┘            │
│         │                       │                       │
│         └───────────┬───────────┘                       │
│                     ▼                                   │
│              PublishBinding                              │
│              (pre-resolved                               │
│               execution slot)                            │
│                     │                                   │
│         ┌───────────┴───────────────┐                   │
│         │ endpoint*   (non-owning)  │                   │
│         │ write_fn    (fn pointer)  │                   │
│         │ format_fn   (fn pointer)  │                   │
│         │ filter      (enum)        │                   │
│         │ enabled     (bool)        │                   │
│         └───────────────────────────┘                   │
│                     │                                   │
│                     │ atomic<const PublishBinding*>      │
│                     │ per PublishId slot                 │
├─────────────────────┼───────────────────────────────────┤
│                     ▼          HOT PATH                 │
│                                                         │
│  Worker thread:                                         │
│    1. pop LogRecord                                     │
│    2. call submit_fn(storage)                           │
│         → submit_impl<Stored, PublishId::H1>            │
│    3. load binding ptr (atomic, acquire)                │
│    4. check enabled (1 branch)                          │
│    5. check filter vs tag (1 branch)                    │
│    6. format_fn(raw_view) → formatted                   │
│    7. write_fn(endpoint, formatted)                     │
│    8. done                                              │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## 2. Podział odpowiedzialności

### PublishId — identyfikacja ścieżki

```
enum class PublishId : uint8_t {
    Handler1,
    Handler2,
    Handler3,
    Handler4,
    Count
};
```

Compile-time enum. Każdy Handler ma przypisany PublishId.
PublishId NIE jest MsgTag. MsgTag identyfikuje typ payloadu.
PublishId identyfikuje ścieżkę publikacji.

Jeden Handler ma dokładnie jeden PublishId.
Różne Handlery mogą mieć ten sam PublishId (fan-in) — ale typowo 1:1.

### PublishBinding — runtime execution slot

```
struct PublishBinding {
    using WriteFn  = void(*)(void* endpoint, std::string_view data);
    using FormatFn = std::string(*)(std::string_view raw);

    void*     endpoint;    // non-owning: ofstream*, FILE*, etc.
    WriteFn   write_fn;    // type-erased writer (terminal_write, file_write)
    FormatFn  format_fn;   // type-erased formatter (JsonSink::format, TextSink::format)
    MsgFilter filter;      // All, RequestOnly, ResponseOnly
    bool      enabled;     // active flag
};
```

~40 bajtów. Mieści się w jednej cache line.

Binding NIE jest configiem. Binding NIE jest registry.
Binding jest **pre-resolved execution slot** — zawiera wszystko,
co worker thread potrzebuje do wykonania publish bez żadnego lookupu.

Binding jest **ustawiany w cold path** (init, reconfig).
Binding jest **czytany w hot path** (worker loop).

### OutputRegistry — ownership zasobów

```
class OutputRegistry {
    // owns the actual I/O resources
    std::unordered_map<ChannelId, std::unique_ptr<OutputHandle>> channels_;
};
```

OutputHandle to RAII wrapper na zasób I/O:
- FileHandle: owns std::ofstream
- TerminalHandle: wraps FILE* (stdout/stderr, non-owning)
- (future: SocketHandle, SyslogHandle, etc.)

OutputRegistry:
- tworzy i niszczy zasoby
- mapuje logical ChannelId → fizyczny endpoint
- żyje na cold path
- **NIE jest widoczny z hot path** — hot path widzi tylko `void* endpoint` w bindingu

ChannelId to prosty identyfikator:

```
enum class ChannelId : uint8_t {
    FileA,
    FileB,
    FileC,
    FileD,
    Terminal,
    Count
};
```

### RuntimeConfig — konfiguracja logiczna

```
struct PublishConfig {
    PublishId   publish_id;
    ChannelId   channel;
    MsgFilter   filter;       // All, RequestOnly, ResponseOnly
    bool        enabled;
};
```

RuntimeConfig trzyma tablicę PublishConfig — po jednym per PublishId.
NIE trzyma zasobów. NIE trzyma function pointerów.
Jest czysto deklaratywny.

RuntimeConfig + OutputRegistry → PublishBinding (resolution w cold path).

### MsgFilter — filtr per binding

```
enum class MsgFilter : uint8_t {
    All,
    RequestOnly,
    ResponseOnly
};
```

Worker sprawdza: `if (binding.filter != MsgFilter::All && !matches(envelope.tag, binding.filter)) return;`

To jest 1 branch. Na dobrze predykowanym kodzie — ~0 ns cost.

### Publisher<PublishId> — specjalizacja per slot

Nowa rola Publishera: **nie** instancjonuje Policy.
Publisher jest teraz cienką warstwą, która czyta binding i wywołuje write_fn.

Nie ma już Publisher<PolicyTemplate, Sink>. Jest Publisher<PublishId>.

```
template<PublishId Id>
struct Publisher {
    static void publish(std::string_view raw_view) {
        const PublishBinding* b = binding_slots[Id].load(std::memory_order_acquire);
        if (!b || !b->enabled) return;
        // filter check omitted here — see hot path flow
        std::string formatted = b->format_fn(raw_view);
        b->write_fn(b->endpoint, {formatted.data(), formatted.size()});
    }
};
```

Publisher NIE posiada stanu. Publisher NIE posiada zasobów.
Publisher jest compile-time dispatch point, który deleguje do pre-resolved bindingu.

### Sink (bez zmian)

Sink pozostaje stateless, CRTP-based formatter.
Jedyna zmiana: Sink::format jest teraz dostępny jako function pointer
(static member function jest kompatybilna z `std::string(*)(std::string_view)`).

- JsonSink::format → `FormatFn`
- TextSink::format → `FormatFn`

### Write functions (nowa warstwa — zastępuje Policy::write_impl)

Type-erased write functions, kompatybilne z `void(*)(void*, std::string_view)`:

```
void terminal_write(void* /*ctx*/, std::string_view data) {
    std::fwrite(data.data(), 1, data.size(), stdout);
    std::fflush(stdout);
}

void file_write(void* ctx, std::string_view data) {
    auto* stream = static_cast<std::ofstream*>(ctx);
    stream->write(data.data(), static_cast<std::streamsize>(data.size()));
    stream->flush();
}
```

To jest odpowiednik obecnych Policy::write_impl, ale wyciągnięty
jako wolne funkcje z `void*` context. Brak CRTP. Brak template.
Zwykły function pointer + context pointer.

---

## 3. Przepływ danych

### 3.1. Cold Path — Initialization

```
1. OutputRegistry tworzy zasoby:
     registry.create<FileHandle>(ChannelId::FileA, "/var/log/app_a.log");
     registry.create<FileHandle>(ChannelId::FileB, "/var/log/app_b.log");
     registry.create<FileHandle>(ChannelId::FileC, "/var/log/app_c.log");
     registry.create<FileHandle>(ChannelId::FileD, "/var/log/app_d.log");
     registry.create<TerminalHandle>(ChannelId::Terminal);

2. RuntimeConfig definiuje routing:
     { PublishId::Handler1, ChannelId::FileA, MsgFilter::All,          true }
     { PublishId::Handler2, ChannelId::FileB, MsgFilter::All,          true }
     { PublishId::Handler3, ChannelId::FileC, MsgFilter::RequestOnly,  true }
     { PublishId::Handler4, ChannelId::FileD, MsgFilter::ResponseOnly, true }

3. Resolver tworzy PublishBinding per PublishId:
     Dla każdego PublishConfig:
       a) endpoint = registry.get_endpoint(config.channel)
            → zwraca void* (np. ofstream*)
       b) write_fn = registry.get_write_fn(config.channel)
            → zwraca function pointer (file_write lub terminal_write)
       c) format_fn = resolve_format_fn(config)
            → zwraca Sink::format pointer (JsonSink::format lub TextSink::format)
       d) Tworzy PublishBinding{endpoint, write_fn, format_fn, filter, enabled}
       e) Alokuje binding (cold path — heap OK)
       f) Atomowo zapisuje pointer do binding_slots[PublishId]

4. Bindingi są gotowe. Hot path może działać.
```

### 3.2. Cold Path — Reconfiguration

```
1. Konfiguracja zmienia np. Handler2 → ChannelId::FileC, MsgFilter::RequestOnly

2. Resolver:
   a) Tworzy NOWY PublishBinding z nowymi parametrami
   b) Atomowo swapuje pointer w binding_slots[PublishId::Handler2]
   c) Stary binding NIE jest natychmiast zwalniany (RCU-like)

3. Worker thread:
   - Przy następnym rekordzie z Handler2, załaduje nowy pointer
   - Stary binding jest "retired" — może być zwolniony po pewnym czasie
     lub nigdy (przy 40B per binding, leak jest akceptowalny)
```

### 3.3. Hot Path — Worker Thread

```
1. MpscQueue::pop() → LogRecord* rec

2. rec->submit_fn(rec->storage_ptr())
   │
   └→ submit_impl<StoredEnvelope<RequestPayload>, PublishId::Handler3>
      │
      ├─ 1. Extract envelope reference: auto& env = obj->env;
      │
      ├─ 2. Serialize envelope to string_view:
      │     FixedStringBuf<1024> (thread_local, zero alloc)
      │     env.debug_print(os) → raw_view
      │
      ├─ 3. Load binding (atomic, acquire):
      │     const PublishBinding* b = binding_slots[PublishId::Handler3].load(acquire);
      │
      ├─ 4. Guard: if (!b || !b->enabled) return;
      │     [1 branch, well-predicted]
      │
      ├─ 5. Filter: if (b->filter == MsgFilter::RequestOnly && env.type_id != MsgTag::Request) return;
      │     [1 branch, well-predicted]
      │
      ├─ 6. Format: std::string formatted = b->format_fn(raw_view);
      │     [1 indirect call → JsonSink::format or TextSink::format]
      │     [1 heap alloc — pre-existing cost, nie wprowadzamy nowego]
      │
      ├─ 7. Write: b->write_fn(b->endpoint, formatted_view);
      │     [1 indirect call → file_write or terminal_write]
      │     [actual I/O — dominant cost]
      │
      └─ 8. Done. No lookup. No map. No virtual. No RTTI.

3. rec->destroy_fn(rec->storage_ptr())

4. Recycle to freelist
```

### Koszt hot path vs obecny system:

| Operacja               | Obecny system                    | Nowy system                        |
|------------------------|----------------------------------|------------------------------------|
| Dispatch               | hardcoded (0 cost)               | atomic load + 2 branches (~2 ns)  |
| Format                 | Sink::format (1 alloc)           | format_fn indirect (1 alloc)       |
| Write                  | Policy::write_impl (direct)      | write_fn indirect call             |
| Routing                | brak (1 output)                  | pre-resolved (0 cost)             |
| Łączny overhead        | —                                | ~2-5 ns (atomic + indirect calls)  |

Overhead jest mierzalny ale akceptowalny. Zysk: pełna konfigurowalność.

---

## 4. Mechanizm wejścia PublishId do hot path

### Problem

`submit_fn` ma sygnaturę `void(*)(void*)`. Nie niesie informacji o PublishId.
Worker wywołuje `rec->submit_fn(rec->storage_ptr())` — skąd ma wiedzieć, do którego bindingu?

### Rozwiązanie: Bake PublishId into submit_fn template

W `enqueue()`:

```
rec->submit_fn = &submit_impl<Stored, PubId>;
```

Gdzie `PubId` jest znany w compile-time w momencie enqueue.

To wymaga, żeby Handler miał PublishId jako template parameter:

```
template<PublishId Id>
struct Handler {
    template<MsgTag Tag, typename Engine = LogEngine, typename... Args>
    static void log(Args&&... args) {
        auto payload = Builder::build<Tag>(...);
        auto& engine = Engine::instance();
        engine.enqueue<Id>(std::move(payload));
        //             ^^ PublishId propagowany do enqueue
    }
};
```

LogEngine::enqueue staje się:

```
template<PublishId Id, typename Envelope>
void enqueue(Envelope&& env) {
    // ... acquire record, placement new ...
    rec->submit_fn = &submit_impl<StoredEnvelope<E>, Id>;
    // ... push to queue ...
}
```

`submit_impl` staje się:

```
template<typename Stored, PublishId Id>
static void submit_impl(void* storage) {
    // ... serialize envelope ...
    Publisher<Id>::publish(raw_view);
}
```

**Koszt**: zero. PublishId jest template parameter — resolwowany w compile-time.
Każda kombinacja (Stored, PublishId) generuje osobną instancję submit_impl.
Przy 2 typach envelope × 4 publisherów = 8 instancji. Akceptowalne.

**Alternatywa odrzucona**: Przechowywanie PublishId w LogRecord jako pole runtime.
Wymagałoby switcha lub tablicy function pointerów w worker loop. Dodatkowy branch.
Niepotrzebne, skoro PublishId jest znany w compile-time.

---

## 5. Binding slots — storage

### Globalna tablica atomowych pointerów

```
inline std::array<
    std::atomic<const PublishBinding*>,
    static_cast<size_t>(PublishId::Count)
> binding_slots{};
```

Każdy slot to atomic pointer do immutable PublishBinding.

Cechy:
- Indeksowanie: `binding_slots[static_cast<size_t>(Id)]` — O(1)
- Rozmiar: `Count * 8B` = 32B dla 4 slotów — mieści się w 1 cache line
- Atomic load: acquire semantics, ~1 ns na ARM64/x86
- Brak false sharing: cała tablica w jednej cache line (read-only w hot path)

### Alternatywa odrzucona: per-Publisher static member

```
template<PublishId Id>
struct Publisher {
    static std::atomic<const PublishBinding*> binding_;
};
```

Problem: każda specjalizacja to osobna zmienna → mogą wylądować
w różnych cache lines → gorszy locality niż jedna tablica.

### Dlaczego NIE SeqLock

SeqLock na binding struct:
- Technicznie poprawny schemat
- Ale: C++ standard mówi, że read non-atomic data racing with write = UB
- Nawet jeśli na ARM64/x86 działa poprawnie
- Nie warto — atomic pointer swap jest prostszy i standardowo poprawny

### Dlaczego NIE double-buffer per slot

- Problem z rapid successive reconfigures (2 swapy zanim worker przeczyta)
- Komplikacja bez zysku — binding jest 40B, alokacja w cold path jest OK

---

## 6. Lifetime endpointów

### Problem

Binding trzyma `void* endpoint` (np. `ofstream*`).
Jeśli OutputRegistry zamknie/usunie zasób, pointer w bindingu staje się dangling.

### Reguła

**Endpoint MUSI żyć dłużej niż binding, który na niego wskazuje.**

### Enforcement

OutputRegistry jest jedynym ownerem zasobów.
Sekwencja zamknięcia kanału:

```
1. Dla każdego PublishId używającego tego kanału:
   a) Utwórz nowy binding z endpoint=nullptr, enabled=false
   b) Atomowo swap w binding_slots[Id]
   c) (opcjonalnie) Wymuś memory fence

2. Poczekaj, aż worker przetworzy bieżący rekord
   (worker jest single-threaded → po przetworzeniu 1 rekordu,
    stary binding pointer nie jest już używany)

3. Teraz bezpiecznie zamknij zasób w OutputRegistry

4. (Opcjonalnie) Zwolnij stary binding — lub zostaw (leak ~40B)
```

### Uproszczenie dla V1

W pierwszej wersji: OutputRegistry żyje dłużej niż LogEngine.
Zasoby są tworzone przy starcie, niszczone po shutdown.
Nie ma dynamicznego zamykania kanałów w trakcie pracy.

To eliminuje 100% problemów z lifetime w V1.
Dynamiczne zamykanie to V2 feature z powyższą sekwencją.

---

## 7. Scenariusz testowy — 4 handlery

```
H1 → PublishId::Handler1 → MsgFilter::All          → ChannelId::FileA → /tmp/a.log
H2 → PublishId::Handler2 → MsgFilter::All          → ChannelId::FileB → /tmp/b.log
H3 → PublishId::Handler3 → MsgFilter::RequestOnly  → ChannelId::FileC → /tmp/c.log
H4 → PublishId::Handler4 → MsgFilter::ResponseOnly → ChannelId::FileD → /tmp/d.log
```

### Init (cold path):

```
// 1. Zasoby
OutputRegistry registry;
registry.open_file(ChannelId::FileA, "/tmp/a.log");
registry.open_file(ChannelId::FileB, "/tmp/b.log");
registry.open_file(ChannelId::FileC, "/tmp/c.log");
registry.open_file(ChannelId::FileD, "/tmp/d.log");

// 2. Bindingi
resolve_and_install(PublishId::Handler1, ChannelId::FileA, MsgFilter::All, TextSink::format);
resolve_and_install(PublishId::Handler2, ChannelId::FileB, MsgFilter::All, TextSink::format);
resolve_and_install(PublishId::Handler3, ChannelId::FileC, MsgFilter::RequestOnly, JsonSink::format);
resolve_and_install(PublishId::Handler4, ChannelId::FileD, MsgFilter::ResponseOnly, JsonSink::format);
```

### Runtime (hot path):

```
Handler<PublishId::Handler1>::log<MsgTag::Request>(severity, ts, tid, rid, cid, mid, sv, uid, path);
  → enqueue<PublishId::Handler1>(RequestPayload{...})
    → submit_impl<Stored, PublishId::Handler1>
      → load binding_slots[0] → binding z FileA, MsgFilter::All
      → filter check: All → pass
      → format: TextSink::format(raw_view) → formatted
      → write: file_write(ofstream_a*, formatted) → write to /tmp/a.log

Handler<PublishId::Handler3>::log<MsgTag::Request>(severity, ts, tid, rid, cid, mid, sv, uid, path);
  → enqueue<PublishId::Handler3>(RequestPayload{...})
    → submit_impl<Stored, PublishId::Handler3>
      → load binding_slots[2] → binding z FileC, MsgFilter::RequestOnly
      → filter check: RequestOnly vs Request → pass
      → format + write → /tmp/c.log

Handler<PublishId::Handler3>::log<MsgTag::Generic>(severity, ts, tid, rid, cid, mid, sv);
  → enqueue<PublishId::Handler3>(GenericPayload{...})
    → submit_impl<Stored, PublishId::Handler3>
      → load binding_slots[2] → binding z FileC, MsgFilter::RequestOnly
      → filter check: RequestOnly vs Generic → DROP (Generic is neither Request nor Response)
```

Zero lookup w runtime. Każdy handler trafia do swojego bindingu przez indeks tablicy.

---

## 8. Co się dzieje z istniejącymi komponentami

| Komponent              | Status    | Co się zmienia                                                       |
|------------------------|-----------|----------------------------------------------------------------------|
| LogRecord              | BEZ ZMIAN | Bez zmian — storage, function pointers, alignment                    |
| FreeList               | BEZ ZMIAN | Bez zmian                                                            |
| MpscQueue              | BEZ ZMIAN | Bez zmian                                                            |
| LogEngine::enqueue     | ZMIANA    | Nowy template param PublishId                                        |
| LogEngine::submit_impl | ZMIANA    | Nowy template param PublishId, deleguje do Publisher<Id>              |
| Handler                | ZMIANA    | Template param PublishId, propaguje do enqueue                       |
| Builder / PayloadReg   | BEZ ZMIAN | Bez zmian — compile-time payload construction                       |
| Publisher              | REWRITE   | Z Publisher<Policy,Sink> na Publisher<PublishId> z binding lookup     |
| PolicyBase             | USUNIĘTY  | Zastąpiony przez write functions (terminal_write, file_write)        |
| TerminalPolicy         | USUNIĘTY  | Zastąpiony przez terminal_write + TerminalHandle                     |
| FilePolicy             | USUNIĘTY  | Zastąpiony przez file_write + FileHandle (owned by OutputRegistry)   |
| SinkBase/JsonSink/Text | BEZ ZMIAN | Sink::format staje się function pointer w bindingu                   |
| OutputRegistry         | NOWY      | Owns I/O resources, maps ChannelId → endpoint                       |
| PublishBinding         | NOWY      | Pre-resolved execution slot                                          |
| RuntimeConfig          | NOWY      | Deklaratywny config: PublishId → (channel, filter, enabled)          |
| binding_slots          | NOWY      | Globalna tablica atomic<const PublishBinding*>                       |

---

## 9. Trade-offs

### 9.1. Compile-time PublishId (enum) vs dynamic registry

**Wybór: enum (compile-time)**

Zalety:
- Zero-cost dispatch: PublishId jest template param → baked into submit_fn
- Tablica binding_slots indeksowana enumem → O(1)
- Compiler może inlinować submit_impl per PublishId
- Brak string lookup, brak hash map

Wady:
- Dodanie nowego handlera wymaga rozszerzenia enum i recompile
- Maksymalna liczba slotów znana w compile-time

Ocena: akceptowalne. System logowania ma stałą topologię.
Nowe handlery to zmiana architekturalna, nie runtime event.

### 9.2. Atomic pointer swap vs SeqLock vs mutex

**Wybór: atomic pointer swap (RCU-like)**

Zalety:
- Standardowo poprawne (brak UB)
- Proste: 1 atomic load w hot path, 1 atomic store w cold path
- Brak contention: pointer swap jest wait-free

Wady:
- Stare bindingi nie są natychmiast zwalniane
- Teoretyczny "memory leak" ~40B per reconfig per slot

Ocena: przy 4 slotach i ~10 reconfig na sesję = ~1.6 KB "leaku". Negligible.
Można dodać epoch-based reclamation w V2 jeśli potrzeba.

### 9.3. Type-erased write_fn vs CRTP Policy

**Wybór: function pointer + void* context**

Zalety:
- Prosty model ownership: OutputRegistry owns resource, binding has non-owning ptr
- Brak template explosion: 1 write function per output type (nie per Sink×Policy)
- Łatwe dodawanie nowych output typów (socket, syslog) — nowa funkcja, nie nowy template

Wady:
- Indirect call (~1 ns overhead vs direct call)
- Utrata inliningu write_impl

Ocena: write_impl robi I/O (fwrite/ofstream::write). I/O kosztuje ~microseconds.
1 ns overhead na indirect call jest immeasurable vs I/O cost.
CRTP inlining write_impl nie daje żadnego zysku jeśli ciało to fwrite().

### 9.4. Jedna globalna binding_slots tablica vs per-Publisher static

**Wybór: jedna globalna tablica**

Zalety:
- Wszystkie sloty w jednej/dwóch cache lines (32B przy 4 slotach)
- Lepszy spatial locality — worker iteruje przez rekordy z różnych handlerów
- Prostszy do inicjalizacji i zarządzania

Wady:
- False sharing w teoriI jeśli config i worker piszą/czytają jednocześnie.
  ALE: config pisze do nowego obiektu, potem store pointer.
  Worker czyta pointer. Brak false sharing na pointer (read-only w hot path,
  write jest do innego obiektu).

### 9.5. Format allocation (std::string z Sink::format)

**Status quo: nie zmieniamy w tym refactorze**

Sink::format() zwraca std::string. To jest heap alloc na worker thread.
Jest to pre-existing cost, obecny w systemie od zawsze.

Optymalizacja (V2): FixedStringBuf dla format output.
Ale to ortogonalny problem, nie związany z architekturą routingu.

---

## 10. Analiza ryzyk

### 10.1. Dangling endpoint pointer po reconfig

**Ryzyko**: Config zamyka kanał → ofstream destroyed → binding nadal ma stary pointer.

**Mitigation**: Sekwencja zamknięcia (sekcja 6): najpierw swap binding na nullptr,
potem czekaj, potem zamknij zasób. V1: zasoby żyją do shutdown.

**Severity**: High jeśli zignorowane. Eliminated w V1 przez prostszy lifecycle.

### 10.2. Data race na PublishBinding

**Ryzyko**: Worker czyta binding, config pisze nowy binding.

**Mitigation**: Binding jest immutable po utworzeniu. Config tworzy NOWY binding
i atomowo swapuje pointer. Worker czyta pointer atomowo.
Brak race: worker nigdy nie czyta obiektu, który jest modyfikowany.

**Severity**: Eliminated by design (immutable binding + atomic pointer).

### 10.3. Stale binding po shutdown

**Ryzyko**: Worker drainuje queue po shutdown, bindingi mogą być w trakcie teardown.

**Mitigation**: Teardown sequence:
1. LogEngine::shutdown() → worker drains queue
2. Worker exits
3. THEN OutputRegistry tears down resources
4. THEN binding_slots are cleared

Tak długo jak OutputRegistry żyje dłużej niż LogEngine, brak problemu.
Egzekucja: OutputRegistry tworzony PRZED LogEngine::ensure_running(),
niszczony PO LogEngine::shutdown().

### 10.4. Cache locality

**Ryzyko**: Indirect calls (write_fn, format_fn) powodują cache miss na code path.

**Mitigation**: Worker thread wywołuje te same function pointers w kółko
(np. file_write dla Handler1, file_write dla Handler2).
Instruction cache utrzyma oba warianty (terminal_write, file_write) po kilku iteracjach.
Binding_slots tablica mieści się w L1 cache.

**Severity**: Negligible after warmup.

### 10.5. False sharing na binding_slots

**Ryzyko**: Config thread pisze do binding_slots[i], worker czyta binding_slots[j],
oba w tej samej cache line → false sharing.

**Analiza**: Config thread pisze POINTER (8B), nie binding struct.
Ale zapis do binding_slots[i] invalidates cache line dla czytelnika binding_slots[j].

**Mitigation**: Reconfiguracja jest rzadka (~raz na sesję).
Koszt jednego cache line invalidation = ~100 ns.
Akceptowalne.

Jeśli by się okazało problemem: `alignas(64)` per slot (padding).
Ale to overkill dla 4 slotów z rzadkimi writes.

### 10.6. submit_impl template explosion

**Ryzyko**: Każda kombinacja (EnvelopeType, PublishId) generuje osobną instancję.

**Analiza**: 2 envelope types × 4 PublishIds = 8 instancji.
Każda to ~20 instrukcji. Łączny koszt: ~640B code. Negligible.

Gdyby system miał 50 envelope types × 20 PublishIds = 1000 instancji — problem.
Ale to nie jest ten system.

---

## 11. Diagram zależności (compile-time)

```
PublishId (enum)
    │
    ├──→ Handler<PublishId>          uses enum as template param
    │        │
    │        └──→ LogEngine::enqueue<PublishId>
    │                  │
    │                  └──→ submit_impl<Stored, PublishId>
    │                            │
    │                            └──→ Publisher<PublishId>::publish
    │                                      │
    │                                      └──→ binding_slots[Id].load()
    │                                                │
    │                                                └──→ PublishBinding
    │                                                      ├── write_fn → I/O
    │                                                      └── format_fn → Sink
    │
    ├──→ RuntimeConfig              uses enum as key
    │        │
    │        └──→ Resolver          maps config → binding
    │                │
    │                └──→ OutputRegistry    resolves ChannelId → endpoint
    │
    └──→ binding_slots[]            indexed by enum
```

---

## 12. Podsumowanie — co rozwiązuje ta architektura

| Problem                                    | Rozwiązanie                                      |
|--------------------------------------------|--------------------------------------------------|
| FilePolicy open/write/close per log        | OutputRegistry owns ofstream, binding has ptr     |
| Hardcoded TerminalPolicy+TextSink          | Per-PublishId binding with function pointers       |
| Brak routingu handler → output             | PublishId → binding_slots[Id] → endpoint           |
| Brak filtrowania per handler               | MsgFilter w PublishBinding, 1 branch w hot path    |
| Brak runtime reconfiguration               | Atomic pointer swap w binding_slots                |
| Mixed responsibilities                     | Config / Registry / Binding / Publisher separated  |
| ofstream w Policy (ownership problem)      | OutputRegistry owns, binding borrows               |
| Virtual dispatch w hot path                | Function pointers + void* context                  |
| Lookup w hot path                          | Pre-resolved binding, array index access           |

---

## Awaiting Review

Cała architektura jest do przeglądu.
Brak kodu do implementacji — najpierw akceptacja modelu.

Kluczowe decyzje do zatwierdzenia:

1. [ ] PublishId jako template param Handler (compile-time routing)
2. [ ] PublishBinding jako immutable struct z function pointers
3. [ ] binding_slots jako globalna tablica atomic pointerów
4. [ ] OutputRegistry jako RAII owner zasobów I/O
5. [ ] Eliminacja PolicyBase/TerminalPolicy/FilePolicy na rzecz write functions
6. [ ] Atomic pointer swap (RCU-like) dla reconfiguration
7. [ ] MsgFilter enum z single-branch check w hot path
8. [ ] V1: static lifecycle (zasoby żyją do shutdown), V2: dynamic close sequence
