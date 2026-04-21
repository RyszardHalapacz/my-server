#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/publisher_runtime.hpp"
#include "publisher/runtime/registration_handle.hpp"
#include "publisher/runtime/resource_store.hpp"
#include "publisher/runtime/sink_traits.hpp"
#include "publisher/runtime/token_registry.hpp"

using namespace publisher::core;
using namespace publisher::runtime;

// ─── Helper ──────────────────────────────────────────────────────

namespace {
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
    EXPECT_EQ(reg.freeChannelCount(), 3);
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
    EXPECT_EQ(reg.freeChannelCount(), 3);

    reg.release(t1);
    EXPECT_EQ(reg.freeChannelCount(), 3);

    reg.release(t2);
    EXPECT_EQ(reg.freeChannelCount(), 4);
}

TEST(TokenRegistryTest, GroupResetsAfterLastRelease)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(ChannelGroup::Group0);
    reg.release(t0);

    EXPECT_EQ(reg.groupChannel(ChannelGroup::Group0), TokenRegistry::kNoChannel);

    auto t1 = reg.acquire(ChannelGroup::Group0);
    EXPECT_NE(t1.value, kInvalidToken.value);
}

// ─── TokenRegistry: Mixed Exclusive + Groups ─────────────────────

TEST(TokenRegistryTest, ExclusiveAndGroupsCoexist)
{
    TokenRegistry reg;
    auto te  = reg.acquire();
    auto tg0 = reg.acquire(ChannelGroup::Group1);
    auto tg1 = reg.acquire(ChannelGroup::Group1);

    EXPECT_NE(reg.resolve(te), reg.resolve(tg0));
    EXPECT_EQ(reg.resolve(tg0), reg.resolve(tg1));
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
        auto ch = reg.resolve(h1.token());
        EXPECT_EQ(reg.channelRefCount(ch), 1);
        return ch;
    }();
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

    h1.reassign();
    EXPECT_TRUE(h1.valid());
    EXPECT_EQ(reg.channelRefCount(groupCh), 1);
    EXPECT_NE(reg.resolve(h1.token()), groupCh);
}

TEST(RegistrationHandleTest, ReassignFromExclusiveToGroup)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg);
    auto exclCh = reg.resolve(h1.token());
    EXPECT_EQ(reg.channelRefCount(exclCh), 1);

    RegistrationHandle h2(reg, ChannelGroup::Group1);
    auto groupCh = reg.resolve(h2.token());

    h1.reassign(ChannelGroup::Group1);
    EXPECT_TRUE(h1.valid());
    EXPECT_EQ(reg.channelRefCount(exclCh), 0);
    EXPECT_EQ(reg.resolve(h1.token()), groupCh);
    EXPECT_EQ(reg.channelRefCount(groupCh), 2);
}

TEST(RegistrationHandleTest, ReassignFromGroupToAnotherGroup)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg, ChannelGroup::Group0);
    RegistrationHandle h2(reg, ChannelGroup::Group0);
    auto ch0 = reg.resolve(h1.token());

    h1.reassign(ChannelGroup::Group1);
    EXPECT_TRUE(h1.valid());
    EXPECT_EQ(reg.channelRefCount(ch0), 1);
    EXPECT_NE(reg.resolve(h1.token()), ch0);
}

TEST(RegistrationHandleTest, ReassignLastUserReleasesGroupChannel)
{
    TokenRegistry reg;
    EXPECT_EQ(reg.freeChannelCount(), 4);

    RegistrationHandle h(reg, ChannelGroup::Group0);
    EXPECT_EQ(reg.freeChannelCount(), 3);

    h.reassign();
    EXPECT_EQ(reg.freeChannelCount(), 3);
    EXPECT_EQ(reg.groupChannel(ChannelGroup::Group0), TokenRegistry::kNoChannel);
}

// ─── SinkTraits<Terminal> ────────────────────────────────────────

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

// ─── SinkTraits<File> ────────────────────────────────────────────

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

// ─── PublisherRuntime<Terminal> ──────────────────────────────────

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

// ─── PublisherRuntime<File> ──────────────────────────────────────

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
