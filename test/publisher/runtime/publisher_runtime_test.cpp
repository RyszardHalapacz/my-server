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

// ─── TokenRegistry ───────────────────────────────────────────────────────────

TEST(TokenRegistryTest, AcquireReturnsValidToken)
{
    TokenRegistry reg;
    auto tok = reg.acquire(OutputChannel::Channel0);
    EXPECT_NE(tok.value, kInvalidToken.value);
}

TEST(TokenRegistryTest, ResolveReturnsBoundChannel)
{
    TokenRegistry reg;
    auto tok = reg.acquire(OutputChannel::Channel2);
    EXPECT_EQ(reg.resolve(tok), OutputChannel::Channel2);
}

TEST(TokenRegistryTest, TwoTokensHaveDifferentValues)
{
    TokenRegistry reg;
    auto t0 = reg.acquire(OutputChannel::Channel0);
    auto t1 = reg.acquire(OutputChannel::Channel3);
    EXPECT_NE(t0.value, t1.value);
    EXPECT_EQ(reg.resolve(t0), OutputChannel::Channel0);
    EXPECT_EQ(reg.resolve(t1), OutputChannel::Channel3);
}

TEST(TokenRegistryTest, ReleaseAndReacquireReusesSameToken)
{
    TokenRegistry reg;
    auto tok = reg.acquire(OutputChannel::Channel1);
    const uint32_t original_value = tok.value;
    reg.release(tok);
    auto tok2 = reg.acquire(OutputChannel::Channel1);
    EXPECT_EQ(tok2.value, original_value);
}

TEST(TokenRegistryTest, ReleaseInvalidTokenThrows)
{
    TokenRegistry reg;
    EXPECT_THROW(reg.release(kInvalidToken), std::runtime_error);
}

TEST(TokenRegistryTest, ReleaseAlreadyFreeTokenThrows)
{
    TokenRegistry reg;
    auto tok = reg.acquire(OutputChannel::Channel0);
    reg.release(tok);
    EXPECT_THROW(reg.release(tok), std::runtime_error);
}

TEST(TokenRegistryTest, ExceedingCapacityThrows)
{
    TokenRegistry reg;
    std::vector<PublishToken> tokens;
    tokens.reserve(TokenRegistry::kMaxBindings);
    for (std::size_t i = 0; i < TokenRegistry::kMaxBindings; ++i)
        tokens.push_back(reg.acquire(OutputChannel::Channel0));
    EXPECT_THROW(reg.acquire(OutputChannel::Channel0), std::runtime_error);
}

// ─── RegistrationHandle ──────────────────────────────────────────────────────

TEST(RegistrationHandleTest, DefaultConstructedIsInvalid)
{
    RegistrationHandle h;
    EXPECT_FALSE(h.valid());
}

TEST(RegistrationHandleTest, ConstructedWithRegistryIsValid)
{
    TokenRegistry reg;
    RegistrationHandle h(reg, OutputChannel::Channel0);
    EXPECT_TRUE(h.valid());
}

TEST(RegistrationHandleTest, TokenReleasedOnDestruction)
{
    TokenRegistry reg;
    const uint32_t first_value = [&] {
        RegistrationHandle h(reg, OutputChannel::Channel0);
        return h.token().value;
    }();
    // Token was released — next acquire reuses it
    RegistrationHandle h2(reg, OutputChannel::Channel0);
    EXPECT_EQ(h2.token().value, first_value);
}

TEST(RegistrationHandleTest, MoveSourceBecomesInvalid)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg, OutputChannel::Channel0);
    RegistrationHandle h2 = std::move(h1);
    EXPECT_FALSE(h1.valid());
    EXPECT_TRUE(h2.valid());
}

TEST(RegistrationHandleTest, MoveAssignmentReleasesDestination)
{
    TokenRegistry reg;
    RegistrationHandle h1(reg, OutputChannel::Channel0);
    RegistrationHandle h2(reg, OutputChannel::Channel1);
    const uint32_t h1_value = h1.token().value;
    h2 = std::move(h1);
    EXPECT_EQ(h2.token().value, h1_value);
    EXPECT_FALSE(h1.valid());
}

// ─── SinkTraits<Terminal> ────────────────────────────────────────────────────

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

// ─── SinkTraits<File> ────────────────────────────────────────────────────────

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

// ─── PublisherRuntime<Terminal> ──────────────────────────────────────────────

TEST(PublisherRuntimeTerminalTest, PublishViewWritesToBoundChannel)
{
    TokenRegistry reg;
    OutputResourceStore store;

    std::ostringstream oss0, oss1;
    store.terminals[toIndex(OutputChannel::Channel0)].out = &oss0;
    store.terminals[toIndex(OutputChannel::Channel1)].out = &oss1;

    auto tok = reg.acquire(OutputChannel::Channel0);
    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, tok, "test_data");

    EXPECT_EQ(oss0.str(), "test_data");
    EXPECT_EQ(oss1.str(), "");
}

TEST(PublisherRuntimeTerminalTest, PublishViewRoutesToCorrectChannel)
{
    TokenRegistry reg;
    OutputResourceStore store;

    std::ostringstream oss0, oss1;
    store.terminals[toIndex(OutputChannel::Channel0)].out = &oss0;
    store.terminals[toIndex(OutputChannel::Channel1)].out = &oss1;

    auto tok = reg.acquire(OutputChannel::Channel1);
    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, tok, "ch1_data");

    EXPECT_EQ(oss0.str(), "");
    EXPECT_EQ(oss1.str(), "ch1_data");
}

TEST(PublisherRuntimeTerminalTest, TwoPublishersOnSeparateChannelsDoNotInterfere)
{
    TokenRegistry reg;
    OutputResourceStore store;

    std::ostringstream oss0, oss1;
    store.terminals[toIndex(OutputChannel::Channel0)].out = &oss0;
    store.terminals[toIndex(OutputChannel::Channel1)].out = &oss1;

    auto t0 = reg.acquire(OutputChannel::Channel0);
    auto t1 = reg.acquire(OutputChannel::Channel1);

    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t0, "A");
    PublisherRuntime<SinkKind::Terminal>::publish_view(reg, store, t1, "B");

    EXPECT_EQ(oss0.str(), "A");
    EXPECT_EQ(oss1.str(), "B");
}

// ─── PublisherRuntime<File> ──────────────────────────────────────────────────

TEST(PublisherRuntimeFileTest, PublishViewWritesToBoundFile)
{
    const std::string path = "publisher_runtime_file_test.log";
    std::fstream f(path, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(f.is_open());

    TokenRegistry reg;
    OutputResourceStore store;
    store.files[toIndex(OutputChannel::Channel0)].file = &f;

    auto tok = reg.acquire(OutputChannel::Channel0);
    PublisherRuntime<SinkKind::File>::publish_view(reg, store, tok, "runtime_file");

    f.flush();
    f.close();

    std::ifstream in(path);
    const std::string content(std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>{});
    EXPECT_EQ(content, "runtime_file");
}
