# Token Registration & Channel Flow — krok po kroku

---

## Krok 0: Typy bazowe

### PublishToken (`publish_token.hpp:11-14`)

```cpp
struct PublishToken {
    std::uint32_t value{};       // default: 0
};

inline constexpr PublishToken kInvalidToken{    // sentinel = 4294967295
    static_cast<std::uint32_t>(-1)
};
```

- Token to wrapper na `uint32_t` — 4 bajty, value type.
- `kInvalidToken` to sentinel oznaczający "brak tokenu".

### OutputChannel (`publisher_types.hpp:12-19`)

```cpp
enum class OutputChannel : std::size_t {
    Channel0 = 0, Channel1, Channel2, Channel3, Count   // Count = 4
};
```

- 4 kanaly wyjsciowe, kazdy to indeks do tablicy sinkow.

### Sink handles (`sink_handles.hpp:13-26`)

```cpp
struct TerminalHandle { std::ostream* out{}; };    // nullptr domyslnie
struct FileHandle     { std::fstream* file{}; };
struct SocketHandle   { int fakeFd{-1}; };
```

- Raw pointery — non-owning, podpinane recznie.

---

## Krok 1: Tworzenie `TokenRegistry`

```cpp
TokenRegistry registry;   // default ctor
```

### Stan po konstrukcji (`token_registry.hpp:82-89`):

```
nextToken_              = 0
freeCount_              = 0
tokenToChannel_[256]    — zero-initialized (Channel0)
tokenUsed_[256]         — wszystko false
freeTokens_[256]        — zero-initialized
```

Trzy `std::array` o stalym rozmiarze 256. Zero alokacji heap.

---

## Krok 2: Tworzenie `OutputResourceStore`

```cpp
OutputResourceStore store;   // aggregate init
```

### Stan (`resource_store.hpp:18-23`):

```
store.terminals[4] = { {nullptr}, {nullptr}, {nullptr}, {nullptr} }
store.files[4]     = { {nullptr}, {nullptr}, {nullptr}, {nullptr} }
store.sockets[4]   = { {-1},     {-1},      {-1},      {-1}      }
```

4 sloty na kanal * 3 rodzaje sinkow = 12 uchwytow. Wszystko puste.

---

## Krok 3: Podpiecie sinkow do kanalow

```cpp
store.terminals[toIndex(OutputChannel::Channel0)].out = &std::cout;
```

### Co robi `toIndex` (`publisher_types.hpp:28-31`):

```cpp
constexpr std::size_t toIndex(OutputChannel channel) noexcept {
    return static_cast<std::size_t>(channel);   // Channel0 → 0
}
```

Wynik: `store.terminals[0].out = &std::cout` — Channel0 ma podpiety terminal.

---

## Krok 4: `RegistrationHandle` — rejestracja tokenu

```cpp
RegistrationHandle handle(registry, OutputChannel::Channel0);
```

### Konstruktor (`registration_handle.hpp:19-22`):

```cpp
RegistrationHandle(TokenRegistry& registry, OutputChannel channel)
    : registry_(&registry),              // (1) zapamietuje wskaznik do registry
      token_(registry.acquire(channel))  // (2) woła acquire → dostaje token
{}
```

### Co robi `registry.acquire(Channel0)` (`token_registry.hpp:24-46`):

```cpp
PublishToken acquire(OutputChannel channel)
{
    PublishToken token{};

    if (freeCount_ > 0)           // (A) jest recyklingowany token?
    {                             //     freeCount_ = 0 → NIE
        token = freeTokens_[--freeCount_];
    }
    else
    {
        if (nextToken_ >= 256)    // (B) overflow check → 0 < 256 → OK
            throw ...;

        token = PublishToken{nextToken_++};
        //      token.value = 0, nextToken_ staje sie 1
    }

    tokenToChannel_[token.value] = channel;
    //              [0]          = Channel0     ← mapowanie token→kanal

    tokenUsed_[token.value] = true;
    //         [0]          = true              ← oznaczony jako aktywny

    return token;   // → PublishToken{0}
}
```

### Stan po acquire:

```
nextToken_          = 1
tokenToChannel_[0]  = Channel0
tokenUsed_[0]       = true
```

### Stan `RegistrationHandle` po konstruktorze:

```
handle.registry_ = &registry          (raw pointer, non-owning)
handle.token_    = PublishToken{0}     (kopia z acquire)
```

---

## Krok 4b: Drugi token, ten sam kanal

```cpp
RegistrationHandle handle2(registry, OutputChannel::Channel0);
```

`acquire(Channel0)`:
- `freeCount_ = 0` → nowy slot
- `token = PublishToken{1}`, `nextToken_` = 2
- `tokenToChannel_[1] = Channel0`
- `tokenUsed_[1] = true`

```
Token(0) → Channel0 ─┐
                      ├─→ store.terminals[0] → std::cout    (ten sam sink!)
Token(1) → Channel0 ─┘
```

Wiele tokenow moze wskazywac na ten sam kanal.

---

## Krok 4c: Trzeci token, INNY kanal

```cpp
store.terminals[toIndex(OutputChannel::Channel2)].out = &std::cerr;
RegistrationHandle handle3(registry, OutputChannel::Channel2);
```

`acquire(Channel2)`:
- `token = PublishToken{2}`, `nextToken_` = 3
- `tokenToChannel_[2] = Channel2`

```
Token(0) → Channel0 → store.terminals[0] → std::cout
Token(1) → Channel0 → store.terminals[0] → std::cout
Token(2) → Channel2 → store.terminals[2] → std::cerr
```

---

## Krok 5: Publish — hot path

```cpp
PublisherRuntime<SinkKind::Terminal>::publish_view(
    registry, store, handle.token(), "hello"
);
```

### `handle.token()` (`registration_handle.hpp:56-59`):

```cpp
PublishToken token() const noexcept {
    return token_;    // → PublishToken{0}   (kopia, 4 bajty)
}
```

### `publish_view(...)` (`publisher_runtime.hpp:24-34`):

```cpp
static void publish_view(TokenRegistry& registry,
                         OutputResourceStore& store,
                         PublishToken token,          // {0}
                         std::string_view data)       // "hello"
{
    const auto channel = registry.resolve(token);     // (1)
    const auto idx     = toIndex(channel);            // (2)
    auto& handle       = store.terminals[idx];        // (3)
    SinkTraits<SinkKind::Terminal>::write(handle, data); // (4)
}
```

### (1) `registry.resolve(token)` (`token_registry.hpp:69-74`):

```cpp
OutputChannel resolve(PublishToken token) const noexcept
{
    assert(isValidToken(token));         // 0 < nextToken_(3) → OK
    assert(tokenUsed_[token.value]);     // tokenUsed_[0] = true → OK
    return tokenToChannel_[token.value]; // tokenToChannel_[0] → Channel0
}
```

O(1) — direct array index, noexcept, zero branching w release build.

### (4) `SinkTraits<Terminal>::write(...)` (`sink_traits.hpp:24-28`):

```cpp
static void write(TerminalHandle& handle, std::string_view data) noexcept
{
    assert(handle.out != nullptr);
    handle.out->write(data.data(), static_cast<std::streamsize>(data.size()));
    //    ^^^^
    //    store.terminals[0].out = &std::cout
    //    → std::cout.write("hello", 5)
}
```

### Caly hot path rozwiniety (bez assertow):

```
token.value                         → 0
tokenToChannel_[0]                  → Channel0
static_cast<size_t>(Channel0)      → 0
store.terminals[0].out             → &std::cout
std::cout.write("hello", 5)       → output
```

3 array lookups + 1 write. Zero alokacji, zero branching, zero virtual dispatch.

---

## Krok 6: Destrukcja / zwolnienie tokenu

Kiedy `handle` wychodzi ze scope:

### `~RegistrationHandle()` (`registration_handle.hpp:24-27`):

```cpp
~RegistrationHandle() {
    release();
}
```

### `release()` (`registration_handle.hpp:67-83`):

```cpp
void release() noexcept
{
    if (registry_ != nullptr && token_.value != kInvalidToken.value)
    {
        try {
            registry_->release(token_);    // ← deleguje do registry
        } catch (...) {}                   // destruktor nie rzuca
    }
    registry_ = nullptr;
    token_ = kInvalidToken;                // {4294967295} — uniewaznia
}
```

### `registry.release(token)` (`token_registry.hpp:48-67`):

```cpp
void release(PublishToken token)    // token = {0}
{
    if (!isValidToken(token))       // 0 < nextToken_(3) → valid
        throw ...;

    if (!tokenUsed_[token.value])   // tokenUsed_[0] = true → OK
        throw ...;

    tokenUsed_[token.value] = false;           // tokenUsed_[0] = false
    freeTokens_[freeCount_++] = token;         // freeTokens_[0] = {0}, freeCount_ = 1
}
```

### Stan po release:

```
tokenUsed_[0]    = false          ← slot wolny
freeTokens_[0]   = PublishToken{0} ← na stosie do recyklingu
freeCount_       = 1
```

---

## Krok 7: Recykling tokenu

Nastepny `acquire(Channel3)` wchodzi w galaz recyklingu:

```cpp
if (freeCount_ > 0)                    // freeCount_ = 1 → TAK
{
    token = freeTokens_[--freeCount_]; // freeTokens_[0] = {0}, freeCount_ = 0
}

tokenToChannel_[0] = Channel3;         // token 0 → teraz Channel3
tokenUsed_[0] = true;
```

Ten sam `value=0`, ale prowadzi do `Channel3` zamiast `Channel0`.

---

## Krok 8: Move semantics — transfer ownership

```cpp
RegistrationHandle h1(registry, OutputChannel::Channel0);   // token{3}
RegistrationHandle h2 = std::move(h1);                      // move ctor
```

### Move ctor (`registration_handle.hpp:32-38`):

```cpp
RegistrationHandle(RegistrationHandle&& other) noexcept
    : registry_(other.registry_),       // h2 przejmuje wskaznik
      token_(other.token_)              // h2 przejmuje token{3}
{
    other.registry_ = nullptr;          // h1 → null
    other.token_ = kInvalidToken;       // h1 → sentinel
}
```

### Stan po move:

```
h1: registry_ = nullptr,   token_ = kInvalidToken   ← pusty
h2: registry_ = &registry, token_ = {3}             ← wlasciciel
```

- `h1.~RegistrationHandle()` → `release()` → nullptr check → nic nie robi
- `h2.~RegistrationHandle()` → `release()` → `registry->release({3})` → zwalnia token

### Move assignment (`registration_handle.hpp:40-54`):

```cpp
RegistrationHandle& operator=(RegistrationHandle&& other) noexcept
{
    if (this != &other)
    {
        release();                      // najpierw zwalnia SWOJ stary token

        registry_ = other.registry_;    // przejmuje nowy
        token_ = other.token_;

        other.registry_ = nullptr;      // uniewazniam zrodlo
        other.token_ = kInvalidToken;
    }
    return *this;
}
```

Wazne: `release()` przed przejęciem — nie gubi starego tokenu.

---

## Diagram pelnego flow

```
          COLD PATH                           HOT PATH
          ─────────                           ────────

  TokenRegistry registry;
  OutputResourceStore store;
  store.terminals[0].out = &cout;
           │
           ▼
  RegistrationHandle(registry, Channel0)
           │
           ├─ registry_ = &registry
           └─ token_ = registry.acquire(Channel0)
                          │
                          ├─ token = {nextToken_++}       ← O(1)
                          ├─ tokenToChannel_[v] = ch      ← O(1)
                          ├─ tokenUsed_[v] = true         ← O(1)
                          └─ return token
                                    │
                                    ▼
                          PublisherRuntime<Terminal>
                          ::publish_view(reg, store, token, data)
                                    │
                                    ├─ ch  = reg.resolve(token)         ← O(1)
                                    │       = tokenToChannel_[v]
                                    ├─ idx = toIndex(ch)                ← O(1)
                                    ├─ h   = store.terminals[idx]       ← O(1)
                                    └─ h.out->write(data, len)          ← I/O
           │
           ▼
  ~RegistrationHandle()
           │
           └─ registry_->release(token_)
                          │
                          ├─ tokenUsed_[v] = false
                          └─ freeTokens_[cnt++] = token   ← recykling
```
