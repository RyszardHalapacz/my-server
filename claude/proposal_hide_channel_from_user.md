# Proposal: Hide channels from user — system assigns channels, user picks group

**Date:** 2026-04-05

---

## Original Prompt

Użytkownik nie widzi kanałów. System sam przydziela z free stacka. Kanały mają status Exclusive/Shared. Enum `OutputChannel` zmienia rolę na `ChannelGroup` — logiczny tag grupujący użytkowników. User podaje grupę → system sprawdza czy ktoś już ma taki kanał → dołącza (Shared). User nie podaje grupy → dostaje unikatowy kanał (Exclusive). Shared działa jak shared_ptr — kanał wraca na free stack dopiero gdy ostatni user zwolni. Gdy grupa jest pusta, resetuje się — następny acquire tej grupy dostanie potencjalnie inny kanał.

User może się przerejestrować w trakcie życia:
- Z grupy na Exclusive — release starego (ref--), acquire nowego Exclusive
- Z grupy na inną grupę — release starego (ref--), acquire nowej grupy
- Z Exclusive na grupę — release starego (kanał wraca na stos), acquire grupy
Token grupowy jest likwidowany gdy ostatni user opuści grupę (przez release, reassign, lub destrukcję).

---

## Context

Obecny design wymusza jawny `OutputChannel` przy rejestracji. User nie powinien wiedzieć o kanałach.

Nowy design:
- `OutputChannel` → `ChannelGroup` — logiczny tag, nie fizyczny kanał
- Kanały = tablica + free stack (O(1) alloc/release)
- `acquire()` → Exclusive (nowy kanał)
- `acquire(ChannelGroup)` → system szuka istniejącej grupy, dołącza (Shared) lub alokuje nowy kanał
- Ref counting per kanał — shared_ptr semantyka
- Grupa się resetuje gdy ostatni user zwolni
- `reassign()` / `reassign(ChannelGroup)` — przerejestrowanie bez niszczenia handle'a

---

## Proposed Changes

### Change 1: `publisher_types.hpp` — `OutputChannel` → `ChannelGroup` + nowy `ChannelMode`

**What:**
- Rename `OutputChannel` → `ChannelGroup` (zmiana znaczenia: z fizycznego kanału na logiczny tag)
- Dodanie `ChannelMode` enum (wewnętrzny status kanału)
- `toIndex()` / `toString()` adaptacja

**Why:** Enum teraz oznacza grupę logiczną, nie fizyczny kanał. Fizyczne kanały to indeksy w tablicy zarządzane przez system.

**Impact:** Rename propaguje się wszędzie gdzie był `OutputChannel`. Semantyka się zmienia — to nie jest kanał, to grupa.

**Proposed code:**
```cpp
#ifndef MYSERVER_PUBLISHER_TYPES_HPP
#define MYSERVER_PUBLISHER_TYPES_HPP

#include <cstddef>

namespace publisher::core
{
    enum class ChannelGroup : std::size_t
    {
        Group0 = 0,
        Group1,
        Group2,
        Group3,
        Count
    };

    enum class ChannelMode : bool
    {
        Exclusive = false,
        Shared    = true
    };

    enum class SinkKind : std::size_t
    {
        Terminal = 0,
        File,
        Socket
    };

    [[nodiscard]] constexpr std::size_t toIndex(ChannelGroup group) noexcept
    {
        return static_cast<std::size_t>(group);
    }

    [[nodiscard]] constexpr const char* toString(ChannelGroup group) noexcept
    {
        switch (group)
        {
            case ChannelGroup::Group0: return "Group0";
            case ChannelGroup::Group1: return "Group1";
            case ChannelGroup::Group2: return "Group2";
            case ChannelGroup::Group3: return "Group3";
            default: return "UnknownGroup";
        }
    }

    [[nodiscard]] constexpr const char* toString(SinkKind sink) noexcept
    {
        switch (sink)
        {
            case SinkKind::Terminal: return "Terminal";
            case SinkKind::File:     return "File";
            case SinkKind::Socket:   return "Socket";
            default: return "UnknownSink";
        }
    }
} // namespace publisher::core

#endif // MYSERVER_PUBLISHER_TYPES_HPP
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 2: `token_registry.hpp` — free stack kanałów, grupy, ref counting

**What:** Przebudowa `TokenRegistry`:
- Kanały = indeksy 0..N-1, zarządzane free stackiem (`freeChannels_[]`)
- `acquire()` → Exclusive: pop z free stacka, kanał 1:1
- `acquire(ChannelGroup)` → system sprawdza `groupToChannel_[]`:
  - Grupa ma kanał (ref > 0) → dołącz, ref++, Shared
  - Grupa pusta → pop z free stacka, przypisz do grupy
- `release()` → ref--, gdy ref == 0: kanał wraca na free stack, grupa się resetuje
- `groupToChannel_[]` — mapowanie grupa → kanał (lub sentinel gdy grupa pusta)

**Why:** Kanał to detal wewnętrzny. Free stack = O(1). Grupy to logiczne tagi. Shared_ptr semantyka.

**Impact:** Zmienia publiczne API. Wszyscy callerzy muszą się dostosować.

**Proposed code:**
```cpp
class TokenRegistry
{
public:
    static constexpr std::size_t kMaxBindings = 256;
    static constexpr std::size_t kMaxChannels = 4;
    static constexpr std::size_t kMaxGroups =
        static_cast<std::size_t>(publisher::core::ChannelGroup::Count);
    static constexpr std::size_t kNoChannel = static_cast<std::size_t>(-1);

    TokenRegistry()
    {
        // Free stack — wszystkie kanały wolne
        for (std::size_t i = 0; i < kMaxChannels; ++i)
        {
            freeChannels_[i] = i;
        }
        freeChannelCount_ = kMaxChannels;

        // Grupy — żadna nie ma przypisanego kanału
        groupToChannel_.fill(kNoChannel);
    }

    // ── Publiczne API ─────────────────────────────────────────────

    // Exclusive — unikatowy kanał, bez grupy
    [[nodiscard]] publisher::core::PublishToken acquire()
    {
        const auto channelIdx = popFreeChannel();
        channelRefCount_[channelIdx] = 1;
        channelMode_[channelIdx] = publisher::core::ChannelMode::Exclusive;

        return allocateToken(channelIdx);
    }

    // Grupowy — dołącz do grupy (Shared) lub alokuj nowy kanał dla grupy
    [[nodiscard]] publisher::core::PublishToken acquire(publisher::core::ChannelGroup group)
    {
        const auto groupIdx = static_cast<std::size_t>(group);
        assert(groupIdx < kMaxGroups && "Invalid group index");

        std::size_t channelIdx = groupToChannel_[groupIdx];

        if (channelIdx != kNoChannel && channelRefCount_[channelIdx] > 0)
        {
            // Grupa już ma kanał — dołącz
            ++channelRefCount_[channelIdx];
            channelMode_[channelIdx] = publisher::core::ChannelMode::Shared;
        }
        else
        {
            // Grupa pusta — alokuj nowy kanał
            channelIdx = popFreeChannel();
            channelRefCount_[channelIdx] = 1;
            channelMode_[channelIdx] = publisher::core::ChannelMode::Shared;
            groupToChannel_[groupIdx] = channelIdx;
        }

        return allocateToken(channelIdx);
    }

    void release(publisher::core::PublishToken token)
    {
        if (!isValidToken(token))
        {
            throw std::runtime_error("Attempt to release invalid token");
        }

        if (!tokenUsed_[token.value])
        {
            throw std::runtime_error("Attempt to release already free token");
        }

        if (freeTokenCount_ >= kMaxBindings)
        {
            throw std::runtime_error("Free token pool overflow");
        }

        const auto channelIdx = tokenToChannel_[token.value];
        assert(channelRefCount_[channelIdx] > 0 && "Channel ref count underflow");

        --channelRefCount_[channelIdx];

        if (channelRefCount_[channelIdx] == 0)
        {
            // Ostatni user — kanał wraca na free stack
            pushFreeChannel(channelIdx);

            // Reset grupy która wskazywała na ten kanał
            for (std::size_t g = 0; g < kMaxGroups; ++g)
            {
                if (groupToChannel_[g] == channelIdx)
                {
                    groupToChannel_[g] = kNoChannel;
                    break;
                }
            }
        }

        tokenUsed_[token.value] = false;
        freeTokens_[freeTokenCount_++] = token;
    }

    // resolve — wewnętrzne API dla PublisherRuntime (hot path)
    [[nodiscard]] std::size_t resolve(publisher::core::PublishToken token) const noexcept
    {
        assert(isValidToken(token) && "Invalid publish token");
        assert(tokenUsed_[token.value] && "Inactive publish token");
        return tokenToChannel_[token.value];
    }

    // ── Diagnostyka / testy ──────────────────────────────────────

    [[nodiscard]] std::size_t channelRefCount(std::size_t channelIdx) const noexcept
    {
        assert(channelIdx < kMaxChannels);
        return channelRefCount_[channelIdx];
    }

    [[nodiscard]] std::size_t freeChannelCount() const noexcept
    {
        return freeChannelCount_;
    }

    [[nodiscard]] publisher::core::ChannelMode channelMode(std::size_t channelIdx) const noexcept
    {
        assert(channelIdx < kMaxChannels);
        return channelMode_[channelIdx];
    }

    [[nodiscard]] std::size_t groupChannel(publisher::core::ChannelGroup group) const noexcept
    {
        return groupToChannel_[static_cast<std::size_t>(group)];
    }

private:
    // ── Free stack operacje ──────────────────────────────────────

    [[nodiscard]] std::size_t popFreeChannel()
    {
        if (freeChannelCount_ == 0)
        {
            throw std::runtime_error("No free channels available");
        }
        return freeChannels_[--freeChannelCount_];
    }

    void pushFreeChannel(std::size_t channelIdx)
    {
        assert(freeChannelCount_ < kMaxChannels && "Free channel stack overflow");
        freeChannels_[freeChannelCount_++] = channelIdx;
    }

    // ── Token allocation ─────────────────────────────────────────

    [[nodiscard]] publisher::core::PublishToken allocateToken(std::size_t channelIdx)
    {
        publisher::core::PublishToken token{};

        if (freeTokenCount_ > 0)
        {
            token = freeTokens_[--freeTokenCount_];
        }
        else
        {
            if (nextToken_ >= kMaxBindings)
            {
                throw std::runtime_error("TokenRegistry capacity exceeded");
            }

            token = publisher::core::PublishToken{nextToken_++};
        }

        tokenToChannel_[token.value] = channelIdx;
        tokenUsed_[token.value] = true;

        return token;
    }

    [[nodiscard]] bool isValidToken(publisher::core::PublishToken token) const noexcept
    {
        return token.value < nextToken_;
    }

    // ── Token state ──────────────────────────────────────────────

    std::uint32_t nextToken_{0};

    std::array<std::size_t, kMaxBindings> tokenToChannel_{};
    std::array<bool, kMaxBindings> tokenUsed_{};

    std::array<publisher::core::PublishToken, kMaxBindings> freeTokens_{};
    std::size_t freeTokenCount_{0};

    // ── Channel state ────────────────────────────────────────────

    std::array<std::size_t, kMaxChannels> freeChannels_{};
    std::size_t freeChannelCount_{0};

    std::array<std::size_t, kMaxChannels> channelRefCount_{};
    std::array<publisher::core::ChannelMode, kMaxChannels> channelMode_{};

    // ── Group → Channel mapping ──────────────────────────────────

    std::array<std::size_t, kMaxGroups> groupToChannel_{};
};
```

**Uwaga:** `resolve()` teraz zwraca `std::size_t` (indeks kanału) zamiast `OutputChannel` — bo kanały to indeksy w tablicy, nie wartości enuma. To wymaga drobnej zmiany w `PublisherRuntime`.

#### Review & Status
- [ok ] Awaiting review

---

### Change 3: `resource_store.hpp` — indeksowanie przez `size_t` zamiast `OutputChannel`

**What:** `kChannelCount` zostaje (4), ale store jest teraz indeksowany zwykłym `size_t` (indeks kanału) zamiast `toIndex(OutputChannel)`.

**Why:** Kanały to indeksy, nie wartości enuma.

**Impact:** Minimalny — `kChannelCount` liczy się tak samo, zmiana jest semantyczna.

**Proposed code:**
```cpp
struct OutputResourceStore
{
    static constexpr std::size_t kChannelCount = TokenRegistry::kMaxChannels;

    std::array<TerminalHandle, kChannelCount> terminals{};
    std::array<FileHandle,     kChannelCount> files{};
    std::array<SocketHandle,   kChannelCount> sockets{};
};
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 4: `publisher_runtime.hpp` — `resolve()` zwraca `size_t`

**What:** `publish_view` używa `resolve()` który teraz zwraca `size_t` (indeks kanału). Usuwamy `toIndex()` z hot path.

**Why:** `resolve()` zwraca bezpośrednio indeks — jedno mapowanie mniej.

**Impact:** Minimalny — hot path staje się prostszy (jeden lookup mniej).

**Proposed code (Terminal, analogicznie File/Socket):**
```cpp
template<>
struct PublisherRuntime<publisher::core::SinkKind::Terminal>
{
    static void publish_view(TokenRegistry& registry,
                             OutputResourceStore& store,
                             publisher::core::PublishToken token,
                             std::string_view data) noexcept
    {
        const auto idx = registry.resolve(token);   // → size_t bezpośrednio
        auto& handle = store.terminals[idx];
        SinkTraits<publisher::core::SinkKind::Terminal>::write(handle, data);
    }

    template<typename Derived>
    static void publish(TokenRegistry& registry,
                        OutputResourceStore& store,
                        publisher::core::PublishToken token,
                        const Derived& obj) noexcept
    {
        publish_view(registry, store, token, obj.payload());
    }
};

// Analogicznie File i Socket — jedyna zmiana to usunięcie toIndex().
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 5: `registration_handle.hpp` — dwa konstruktory + `reassign`

**What:**
- Dwa konstruktory: `(registry)` → Exclusive, `(registry, ChannelGroup)` → grupowy
- `reassign()` — przerejestruj na Exclusive (release stary token, acquire nowy)
- `reassign(ChannelGroup)` — przerejestruj do innej grupy (release stary, acquire w nowej grupie)

**Why:** User może zmienić zdanie w trakcie życia — przejść z grupy na exclusive, zmienić grupę, lub z exclusive dołączyć do grupy. Bez `reassign` musiałby niszczyć handle i tworzyć nowy.

**Impact:** Zmiana sygnatury konstruktora + nowe metody. Nie łamie istniejącego kodu bo stary konstruktor nie istnieje (był z `OutputChannel`).

**Proposed code:**
```cpp
class RegistrationHandle
{
public:
    RegistrationHandle() = default;

    // Exclusive — unikatowy kanał
    explicit RegistrationHandle(TokenRegistry& registry)
        : registry_(&registry),
          token_(registry.acquire())
    {}

    // Grupowy — dołącz do grupy
    RegistrationHandle(TokenRegistry& registry, publisher::core::ChannelGroup group)
        : registry_(&registry),
          token_(registry.acquire(group))
    {}

    ~RegistrationHandle()
    {
        release();
    }

    RegistrationHandle(const RegistrationHandle&) = delete;
    RegistrationHandle& operator=(const RegistrationHandle&) = delete;

    RegistrationHandle(RegistrationHandle&& other) noexcept
        : registry_(other.registry_),
          token_(other.token_)
    {
        other.registry_ = nullptr;
        other.token_ = publisher::core::kInvalidToken;
    }

    RegistrationHandle& operator=(RegistrationHandle&& other) noexcept
    {
        if (this != &other)
        {
            release();

            registry_ = other.registry_;
            token_ = other.token_;

            other.registry_ = nullptr;
            other.token_ = publisher::core::kInvalidToken;
        }

        return *this;
    }

    // ── Przerejestrowanie ────────────────────────────────────────

    // Przejdź na Exclusive (release stary token, acquire nowy unikatowy)
    void reassign()
    {
        assert(registry_ != nullptr && "Cannot reassign detached handle");
        release();
        token_ = registry_->acquire();
    }

    // Przejdź do innej grupy (release stary token, acquire w nowej grupie)
    void reassign(publisher::core::ChannelGroup group)
    {
        assert(registry_ != nullptr && "Cannot reassign detached handle");
        release();
        token_ = registry_->acquire(group);
    }

    // ── Accessors ────────────────────────────────────────────────

    [[nodiscard]] publisher::core::PublishToken token() const noexcept
    {
        return token_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return registry_ != nullptr && token_.value != publisher::core::kInvalidToken.value;
    }

private:
    void release() noexcept
    {
        if (registry_ != nullptr && token_.value != publisher::core::kInvalidToken.value)
        {
            try
            {
                registry_->release(token_);
            }
            catch (...)
            {
                // Destructor must not throw.
            }
        }

        token_ = publisher::core::kInvalidToken;
    }

private:
    TokenRegistry* registry_{nullptr};
    publisher::core::PublishToken token_{publisher::core::kInvalidToken};
};
```

**Uwaga:** `release()` nie zeruje `registry_` — bo `reassign` potrzebuje go dalej. Zerowanie `registry_` następuje tylko w move ctor/assign (transfer ownership) i nigdzie indziej. `valid()` sprawdza oba warunki — `registry_ != nullptr` AND `token_ != kInvalidToken`.

#### Review & Status
- [ok ] Awaiting review

---

### Change 6: `log_engine.cpp` — dostosowanie

**What:** Konstruktor bez kanału. Store podpina sinki do wszystkich kanałów po indeksie.

**Why:** Dostosowanie do nowego API.

**Impact:** Minimalny.

**Proposed code:**
```cpp
publisher::runtime::OutputResourceStore& LogEngine::store() noexcept
{
    static publisher::runtime::OutputResourceStore store_ = [] {
        publisher::runtime::OutputResourceStore s{};

        // Podpinamy sinki do WSZYSTKICH kanałów — system sam decyduje który przydzieli
        for (std::size_t i = 0; i < publisher::runtime::OutputResourceStore::kChannelCount; ++i)
        {
            s.terminals[i].out = &std::cout;
        }

        return s;
    }();

    return store_;
}

LogEngine::LogEngine()
    : publishHandle_(registry())     // Exclusive
{
}
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 7: `main.cpp` — dostosowanie

**What:** Nowe API, setup sinków na wszystkich kanałach.

**Why:** Dostosowanie.

**Impact:** Minimalny.

**Proposed code:**
```cpp
TokenRegistry        registry;
OutputResourceStore  store;

for (std::size_t i = 0; i < OutputResourceStore::kChannelCount; ++i)
    store.terminals[i].out = &std::cout;

// Exclusive
RegistrationHandle handleA(registry);

// Grupowy — dołączają do tej samej grupy
RegistrationHandle handleB(registry, ChannelGroup::Group0);
RegistrationHandle handleC(registry, ChannelGroup::Group0);  // ten sam kanał co handleB
```

#### Review & Status
- [ok ] Awaiting review

---

### Change 8: `publisher_runtime_test.cpp` — pełny zestaw testów

**What:** Testy na nowe API + grupy + shared_ptr semantyka.

**Why:** Pokrycie nowej funkcjonalności.

**Impact:** ~20 zmian + nowe testy.

**Proposed code:**

```cpp
// ─── Helper ──────────────────────────────────────────────────────

namespace {
    // Podpina ostringstream do WSZYSTKICH kanałów w store
    struct TestStreams
    {
        static constexpr std::size_t N = TokenRegistry::kMaxChannels;
        std::ostringstream oss[N];

        void bind(OutputResourceStore& store)
        {
            for (std::size_t i = 0; i < N; ++i)
                store.terminals[i].out = &oss[i];
        }

        std::string at(std::size_t i) const { return oss[i].str(); }

        int writtenCount() const
        {
            int c = 0;
            for (std::size_t i = 0; i < N; ++i)
                if (!oss[i].str().empty()) ++c;
            return c;
        }
    };
}

// ─── TokenRegistry: Exclusive ────────────────────────────────────

TEST(TokenRegistryTest, AcquireReturnsValidToken)
{
    TokenRegistry reg;
    auto tok = reg.acquire();
    EXPECT_NE(tok.value, kInvalidToken.value);
}

TEST(TokenRegistryTest, TwoExclusiveTokensGetDifferentChannels)
{
    TokenRegistry reg;
    auto t0 = reg.acquire();
    auto t1 = reg.acquire();
    EXPECT_NE(t0.value, t1.value);
    EXPECT_NE(reg.resolve(t0), reg.resolve(t1));
}

TEST(TokenRegistryTest, ExclusiveReleaseReturnsChannelImmediately)
{
    TokenRegistry reg;
    EXPECT_EQ(reg.freeChannelCount(), 4);

    auto tok = reg.acquire();
    EXPECT_EQ(reg.freeChannelCount(), 3);

    reg.release(tok);
    EXPECT_EQ(reg.freeChannelCount(), 4);
}

TEST(TokenRegistryTest, ReleaseAndReacquireReusesToken)
{
    TokenRegistry reg;
    auto tok = reg.acquire();
    const uint32_t val = tok.value;
    reg.release(tok);
    auto tok2 = reg.acquire();
    EXPECT_EQ(tok2.value, val);
}

TEST(TokenRegistryTest, AllChannelsExclusiveExhausted)
{
    TokenRegistry reg;
    reg.acquire(); reg.acquire(); reg.acquire(); reg.acquire();
    EXPECT_THROW(reg.acquire(), std::runtime_error);
}

TEST(TokenRegistryTest, ReleaseInvalidTokenThrows)
{
    TokenRegistry reg;
    EXPECT_THROW(reg.release(kInvalidToken), std::runtime_error);
}

TEST(TokenRegistryTest, ReleaseAlreadyFreeTokenThrows)
{
    TokenRegistry reg;
    auto tok = reg.acquire();
    reg.release(tok);
    EXPECT_THROW(reg.release(tok), std::runtime_error);
}

// ─── TokenRegistry: Groups ───────────────────────────────────────

TEST(TokenRegistryTest, GroupTokensGetSameChannel)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(ChannelGroup::Group0);
    auto t1 = reg.acquire(ChannelGroup::Group0);
    EXPECT_NE(t0.value, t1.value);
    EXPECT_EQ(reg.resolve(t0), reg.resolve(t1));
}

TEST(TokenRegistryTest, GroupDoesNotConsumeExtraChannel)
{
    TokenRegistry reg;
    EXPECT_EQ(reg.freeChannelCount(), 4);

    auto t0 = reg.acquire(ChannelGroup::Group1);
    EXPECT_EQ(reg.freeChannelCount(), 3);

    auto t1 = reg.acquire(ChannelGroup::Group1);
    EXPECT_EQ(reg.freeChannelCount(), 3);    // nie bierze nowego
}

TEST(TokenRegistryTest, DifferentGroupsGetDifferentChannels)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(ChannelGroup::Group0);
    auto t1 = reg.acquire(ChannelGroup::Group1);
    EXPECT_NE(reg.resolve(t0), reg.resolve(t1));
}

TEST(TokenRegistryTest, GroupRefCountTracksUsers)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(ChannelGroup::Group0);
    auto ch = reg.resolve(t0);
    EXPECT_EQ(reg.channelRefCount(ch), 1);

    auto t1 = reg.acquire(ChannelGroup::Group0);
    EXPECT_EQ(reg.channelRefCount(ch), 2);

    reg.release(t0);
    EXPECT_EQ(reg.channelRefCount(ch), 1);

    reg.release(t1);
    EXPECT_EQ(reg.channelRefCount(ch), 0);
}

TEST(TokenRegistryTest, GroupChannelReturnsToFreeStackOnLastRelease)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(ChannelGroup::Group0);
    auto t1 = reg.acquire(ChannelGroup::Group0);
    auto t2 = reg.acquire(ChannelGroup::Group0);
    EXPECT_EQ(reg.freeChannelCount(), 3);

    reg.release(t0);
    EXPECT_EQ(reg.freeChannelCount(), 3);    // ref > 0

    reg.release(t1);
    EXPECT_EQ(reg.freeChannelCount(), 3);    // ref > 0

    reg.release(t2);
    EXPECT_EQ(reg.freeChannelCount(), 4);    // ref == 0 — wrócił
}

TEST(TokenRegistryTest, GroupResetsAfterLastRelease)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(ChannelGroup::Group0);
    auto ch1 = reg.resolve(t0);
    reg.release(t0);

    // Grupa zresetowana — następny acquire może dostać inny kanał
    EXPECT_EQ(reg.groupChannel(ChannelGroup::Group0), TokenRegistry::kNoChannel);

    auto t1 = reg.acquire(ChannelGroup::Group0);
    // Działa — ale kanał może być inny (lub ten sam, zależy od free stacka)
    EXPECT_NE(t1.value, kInvalidToken.value);
}

// ─── TokenRegistry: Mixed Exclusive + Groups ─────────────────────

TEST(TokenRegistryTest, ExclusiveAndGroupsCoexist)
{
    TokenRegistry reg;
    auto te  = reg.acquire();                           // Exclusive
    auto tg0 = reg.acquire(ChannelGroup::Group1);       // Group1
    auto tg1 = reg.acquire(ChannelGroup::Group1);       // Group1 — ten sam kanał

    EXPECT_NE(reg.resolve(te), reg.resolve(tg0));       // Exclusive != Group1
    EXPECT_EQ(reg.resolve(tg0), reg.resolve(tg1));      // Group1 == Group1
}

// ─── RegistrationHandle ──────────────────────────────────────────

TEST(RegistrationHandleTest, DefaultConstructedIsInvalid)
{
    RegistrationHandle h;
    EXPECT_FALSE(h.valid());
}

TEST(RegistrationHandleTest, ExclusiveIsValid)
{
    TokenRegistry reg;
    RegistrationHandle h(reg);
    EXPECT_TRUE(h.valid());
}

TEST(RegistrationHandleTest, GroupIsValid)
{
    TokenRegistry reg;
    RegistrationHandle h(reg, ChannelGroup::Group0);
    EXPECT_TRUE(h.valid());
}

TEST(RegistrationHandleTest, TokenReleasedOnDestruction)
{
    TokenRegistry reg;
    const uint32_t first_value = [&] {
        RegistrationHandle h(reg);
        return h.token().value;
    }();
    RegistrationHandle h2(reg);
    EXPECT_EQ(h2.token().value, first_value);
}

TEST(RegistrationHandleTest, MoveSourceBecomesInvalid)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg);
    RegistrationHandle h2 = std::move(h1);
    EXPECT_FALSE(h1.valid());
    EXPECT_TRUE(h2.valid());
}

TEST(RegistrationHandleTest, MoveAssignmentReleasesDestination)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg);
    RegistrationHandle h2(reg);
    const uint32_t h1_value = h1.token().value;
    h2 = std::move(h1);
    EXPECT_EQ(h2.token().value, h1_value);
    EXPECT_FALSE(h1.valid());
}

TEST(RegistrationHandleTest, SharedGroupReleasedByLastHandle)
{
    TokenRegistry reg;
    auto ch = [&] {
        RegistrationHandle h1(reg, ChannelGroup::Group0);
        {
            RegistrationHandle h2(reg, ChannelGroup::Group0);
            auto ch = reg.resolve(h1.token());
            EXPECT_EQ(reg.channelRefCount(ch), 2);
        }
        // h2 zniszczony — ref = 1
        auto ch = reg.resolve(h1.token());
        EXPECT_EQ(reg.channelRefCount(ch), 1);
        return ch;
    }();
    // h1 zniszczony — ref = 0, kanał wolny
    EXPECT_EQ(reg.channelRefCount(ch), 0);
}

// ─── RegistrationHandle: reassign ────────────────────────────────

TEST(RegistrationHandleTest, ReassignFromGroupToExclusive)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg, ChannelGroup::Group0);
    RegistrationHandle h2(reg, ChannelGroup::Group0);
    auto groupCh = reg.resolve(h1.token());
    EXPECT_EQ(reg.channelRefCount(groupCh), 2);

    // h1 przechodzi na Exclusive — opuszcza grupę
    h1.reassign();
    EXPECT_TRUE(h1.valid());
    EXPECT_EQ(reg.channelRefCount(groupCh), 1);        // h2 nadal w grupie
    EXPECT_NE(reg.resolve(h1.token()), groupCh);        // h1 ma nowy kanał
}

TEST(RegistrationHandleTest, ReassignFromExclusiveToGroup)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg);                          // Exclusive
    auto exclCh = reg.resolve(h1.token());
    EXPECT_EQ(reg.channelRefCount(exclCh), 1);

    RegistrationHandle h2(reg, ChannelGroup::Group1);    // Group1
    auto groupCh = reg.resolve(h2.token());

    // h1 dołącza do Group1 — zwalnia Exclusive
    h1.reassign(ChannelGroup::Group1);
    EXPECT_TRUE(h1.valid());
    EXPECT_EQ(reg.channelRefCount(exclCh), 0);          // stary kanał wolny
    EXPECT_EQ(reg.resolve(h1.token()), groupCh);         // ten sam kanał co h2
    EXPECT_EQ(reg.channelRefCount(groupCh), 2);
}

TEST(RegistrationHandleTest, ReassignFromGroupToAnotherGroup)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg, ChannelGroup::Group0);
    RegistrationHandle h2(reg, ChannelGroup::Group0);
    auto ch0 = reg.resolve(h1.token());

    // h1 zmienia grupę
    h1.reassign(ChannelGroup::Group1);
    EXPECT_TRUE(h1.valid());
    EXPECT_EQ(reg.channelRefCount(ch0), 1);              // h2 nadal w Group0
    EXPECT_NE(reg.resolve(h1.token()), ch0);              // h1 w nowym kanale
}

TEST(RegistrationHandleTest, ReassignLastUserReleasesGroupChannel)
{
    TokenRegistry reg;
    EXPECT_EQ(reg.freeChannelCount(), 4);

    RegistrationHandle h(reg, ChannelGroup::Group0);
    EXPECT_EQ(reg.freeChannelCount(), 3);

    // Jedyny user w grupie przerejestruje się — kanał grupy wraca na stos
    h.reassign();
    EXPECT_EQ(reg.freeChannelCount(), 3);    // -1 (stary wrócił) -1 (nowy exclusive) = 3
    // Ale grupa się zresetowała
    EXPECT_EQ(reg.groupChannel(ChannelGroup::Group0), TokenRegistry::kNoChannel);
}

// ─── SinkTraits — BEZ ZMIAN ─────────────────────────────────────

TEST(SinkTraitsTerminalTest, WritesToStream)
{
    std::ostringstream oss;
    TerminalHandle handle{&oss};
    SinkTraits<SinkKind::Terminal>::write(handle, "hello");
    EXPECT_EQ(oss.str(), "hello");
}

TEST(SinkTraitsTerminalTest, WritesEmptyView)
{
    std::ostringstream oss;
    TerminalHandle handle{&oss};
    SinkTraits<SinkKind::Terminal>::write(handle, "");
    EXPECT_EQ(oss.str(), "");
}

TEST(SinkTraitsFileTest, WritesToFstream)
{
    const std::string path = "sink_traits_file_test.log";
    std::fstream f(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(f.is_open());

    FileHandle handle{&f};
    SinkTraits<SinkKind::File>::write(handle, "file_data");
    f.flush();
    f.close();

    std::ifstream in(path);
    const std::string content(std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>{});
    EXPECT_EQ(content, "file_data");
}

// ─── PublisherRuntime ────────────────────────────────────────────

TEST(PublisherRuntimeTerminalTest, PublishViewWritesToBoundChannel)
{
    TokenRegistry reg;
    OutputResourceStore store;
    TestStreams ts;
    ts.bind(store);

    auto tok = reg.acquire();
    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, tok, "test_data");

    EXPECT_EQ(ts.writtenCount(), 1);
}

TEST(PublisherRuntimeTerminalTest, TwoExclusivePublishersDoNotInterfere)
{
    TokenRegistry reg;
    OutputResourceStore store;
    TestStreams ts;
    ts.bind(store);

    auto t0 = reg.acquire();
    auto t1 = reg.acquire();

    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t0, "A");
    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t1, "B");

    auto ch0 = reg.resolve(t0);
    auto ch1 = reg.resolve(t1);
    EXPECT_NE(ch0, ch1);
    EXPECT_EQ(ts.at(ch0), "A");
    EXPECT_EQ(ts.at(ch1), "B");
}

TEST(PublisherRuntimeTerminalTest, GroupPublishersWriteToSameChannel)
{
    TokenRegistry reg;
    OutputResourceStore store;
    TestStreams ts;
    ts.bind(store);

    auto t0 = reg.acquire(ChannelGroup::Group0);
    auto t1 = reg.acquire(ChannelGroup::Group0);

    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t0, "X");
    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t1, "Y");

    auto ch = reg.resolve(t0);
    EXPECT_EQ(ts.at(ch), "XY");
    EXPECT_EQ(ts.writtenCount(), 1);
}

TEST(PublisherRuntimeTerminalTest, PublishViewRoutesToCorrectChannel)
{
    TokenRegistry reg;
    OutputResourceStore store;
    TestStreams ts;
    ts.bind(store);

    auto t0 = reg.acquire();
    auto t1 = reg.acquire();

    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t1, "ch1_data");

    auto ch0 = reg.resolve(t0);
    auto ch1 = reg.resolve(t1);
    EXPECT_EQ(ts.at(ch0), "");
    EXPECT_EQ(ts.at(ch1), "ch1_data");
}

TEST(PublisherRuntimeFileTest, PublishViewWritesToBoundFile)
{
    const std::string path = "publisher_runtime_file_test.log";
    std::fstream f(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(f.is_open());

    TokenRegistry reg;
    OutputResourceStore store;
    for (std::size_t i = 0; i < OutputResourceStore::kChannelCount; ++i)
        store.files[i].file = &f;

    auto tok = reg.acquire();
    PublisherRuntime<SinkKind::File>::publish_view(reg, store, tok, "runtime_file");

    f.flush();
    f.close();

    std::ifstream in(path);
    const std::string content(std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>{});
    EXPECT_EQ(content, "runtime_file");
}
```

#### Review & Status
- [ok ] Awaiting review

---

## Build Errors (if any)

_Pending build._

---

## Test Results (if any)

_Pending tests._
