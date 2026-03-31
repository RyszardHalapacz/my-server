# ✅ CLOSED

# Proposal: Testy dla server components (server_types, server_config, server_hooks, command, pipeline)

**Date:** 2026-03-31

---

## Original Prompt

dorzuc testy do tego, znajdz miejsce i daj

---

## Context

Po rozbiciu `server.h` na 5 sub-headerów żaden z nowych modułów nie ma testów.
Istniejące testy serwera (`test/server/`) pokrywają tylko `MetricsMixin`.
Trzeba dodać testy dla: server_types, server_config, server_hooks, command, pipeline.

Miejsce: `test/server/server_components_test.cpp` — zgodnie z konwencją repo (jeden plik na moduł/grupę).

---

## Proposed Changes

### Change 1: `test/server/server_components_test.cpp` — nowy plik testów

**What:** Testy jednostkowe dla wszystkich 5 sub-headerów.
**Why:** Nowe pliki bez pokrycia testami.
**Impact:** Zero wpływu na produkcję. Cold path — tylko testy.

**Proposed code:**
```cpp
#include <gtest/gtest.h>

#include "server/server_types.hpp"
#include "server/server_config.hpp"
#include "server/server_hooks.hpp"
#include "server/command.hpp"
#include "server/pipeline.hpp"

// ============================================================================
// server_types
// ============================================================================

TEST(ServerTypes, EnumsAreDistinct) {
    EXPECT_NE(static_cast<int>(server::ServerState::created),
              static_cast<int>(server::ServerState::running));
    EXPECT_NE(static_cast<int>(server::ServerState::running),
              static_cast<int>(server::ServerState::stopping));
    EXPECT_NE(static_cast<int>(server::ServerState::stopping),
              static_cast<int>(server::ServerState::stopped));
    EXPECT_NE(static_cast<int>(server::ServerState::stopped),
              static_cast<int>(server::ServerState::failed));
}

TEST(ServerTypes, ShutdownModeValues) {
    EXPECT_NE(static_cast<int>(server::ShutdownMode::graceful),
              static_cast<int>(server::ShutdownMode::force));
}

TEST(ServerTypes, SubmitStatusValues) {
    using S = server::SubmitStatus;
    EXPECT_NE(static_cast<int>(S::accepted),       static_cast<int>(S::rejected_full));
    EXPECT_NE(static_cast<int>(S::rejected_full),  static_cast<int>(S::rejected_stopped));
    EXPECT_NE(static_cast<int>(S::rejected_stopped), static_cast<int>(S::invalid));
    EXPECT_NE(static_cast<int>(S::invalid),        static_cast<int>(S::error));
}

TEST(ServerTypes, ProtocolKindValues) {
    using P = server::ProtocolKind;
    EXPECT_NE(static_cast<int>(P::mysql),  static_cast<int>(P::tcp));
    EXPECT_NE(static_cast<int>(P::tcp),    static_cast<int>(P::http));
    EXPECT_NE(static_cast<int>(P::http),   static_cast<int>(P::custom));
}

TEST(ServerTypes, EndpointKindValues) {
    using E = server::EndpointKind;
    EXPECT_NE(static_cast<int>(E::socket),   static_cast<int>(E::file));
    EXPECT_NE(static_cast<int>(E::file),     static_cast<int>(E::terminal));
    EXPECT_NE(static_cast<int>(E::terminal), static_cast<int>(E::custom));
}

// ============================================================================
// server_config
// ============================================================================

TEST(ServerConfig, ListenerConfigDefaults) {
    server::ListenerConfig cfg;
    EXPECT_EQ(cfg.port, 0u);
    EXPECT_EQ(cfg.protocol, server::ProtocolKind::custom);
    EXPECT_TRUE(cfg.enabled);
}

TEST(ServerConfig, ParserConfigDefaults) {
    server::ParserConfig cfg;
    EXPECT_TRUE(cfg.enable_sql_dialect);
    EXPECT_FALSE(cfg.allow_partial_parse);
    EXPECT_EQ(cfg.max_tokens, 0u);
    EXPECT_EQ(cfg.max_input_size, 0u);
}

TEST(ServerConfig, ExecutionConfigDefaults) {
    server::ExecutionConfig cfg;
    EXPECT_EQ(cfg.worker_count, 1u);
    EXPECT_TRUE(cfg.enable_backpressure);
    EXPECT_EQ(cfg.queue_capacity, 1024u);
}

TEST(ServerConfig, PublisherConfigDefaults) {
    server::PublisherConfig cfg;
    EXPECT_TRUE(cfg.enabled);
    EXPECT_FALSE(cfg.publish_raw_event);
    EXPECT_FALSE(cfg.publish_parsed_input);
    EXPECT_FALSE(cfg.publish_command);
    EXPECT_FALSE(cfg.publish_result);
}

TEST(ServerConfig, DistributorConfigDefaults) {
    server::DistributorConfig cfg;
    EXPECT_TRUE(cfg.enabled);
}

TEST(ServerConfig, EndpointConfigDefaults) {
    server::EndpointConfig cfg;
    EXPECT_EQ(cfg.kind, server::EndpointKind::custom);
    EXPECT_TRUE(cfg.enabled);
    EXPECT_TRUE(cfg.name.empty());
}

TEST(ServerConfig, LoggerConfigDefaults) {
    server::LoggerConfig cfg;
    EXPECT_TRUE(cfg.enabled);
    EXPECT_FALSE(cfg.log_submit);
    EXPECT_FALSE(cfg.log_parse);
    EXPECT_FALSE(cfg.log_execute);
    EXPECT_FALSE(cfg.log_publish);
    EXPECT_TRUE(cfg.log_errors);
}

TEST(ServerConfig, ServerConfigDefaultsEmpty) {
    server::ServerConfig cfg;
    EXPECT_TRUE(cfg.listeners.empty());
    EXPECT_TRUE(cfg.endpoints.empty());
}

// ============================================================================
// server_hooks
// ============================================================================

TEST(ServerHooks, MakeNoopHooksAllNonNull) {
    auto h = server::hooks::makeNoopHooks();
    EXPECT_NE(h.on_event_received, nullptr);
    EXPECT_NE(h.on_submit_result,  nullptr);
    EXPECT_NE(h.on_raw_input,      nullptr);
    EXPECT_NE(h.on_parsed_input,   nullptr);
    EXPECT_NE(h.on_command_built,  nullptr);
    EXPECT_NE(h.on_result_ready,   nullptr);
    EXPECT_NE(h.on_distributed,    nullptr);
    EXPECT_NE(h.on_error,          nullptr);
}

TEST(ServerHooks, NoopFunctionsDoNotCrash) {
    Event ev;
    EXPECT_NO_FATAL_FAILURE(server::hooks::noopEvent(ev));
    EXPECT_NO_FATAL_FAILURE(server::hooks::noopText("hello"));
    EXPECT_NO_FATAL_FAILURE(server::hooks::noopStatus(server::SubmitStatus::accepted));
}

TEST(ServerHooks, NoopHooksCanBeCalled) {
    auto h = server::hooks::makeNoopHooks();
    Event ev;
    EXPECT_NO_FATAL_FAILURE(h.on_event_received(ev));
    EXPECT_NO_FATAL_FAILURE(h.on_submit_result(server::SubmitStatus::accepted));
    EXPECT_NO_FATAL_FAILURE(h.on_raw_input("raw"));
    EXPECT_NO_FATAL_FAILURE(h.on_parsed_input("parsed"));
    EXPECT_NO_FATAL_FAILURE(h.on_command_built("cmd"));
    EXPECT_NO_FATAL_FAILURE(h.on_result_ready("result"));
    EXPECT_NO_FATAL_FAILURE(h.on_distributed("dist"));
    EXPECT_NO_FATAL_FAILURE(h.on_error("err"));
}

TEST(ServerHooks, DefaultHookTableHasNullPointers) {
    server::ServerHookTable h{};
    EXPECT_EQ(h.on_event_received, nullptr);
    EXPECT_EQ(h.on_submit_result,  nullptr);
    EXPECT_EQ(h.on_raw_input,      nullptr);
    EXPECT_EQ(h.on_error,          nullptr);
}

// ============================================================================
// command
// ============================================================================

TEST(Command, SelectCommandConstruction) {
    server::command::SelectCommand cmd;
    cmd.table = "users";
    cmd.columns = {"id", "name"};
    EXPECT_EQ(cmd.table, "users");
    ASSERT_EQ(cmd.columns.size(), 2u);
    EXPECT_EQ(cmd.columns[0], "id");
    EXPECT_EQ(cmd.columns[1], "name");
}

TEST(Command, InsertCommandConstruction) {
    server::command::InsertCommand cmd;
    cmd.table = "orders";
    cmd.values = {"1", "item"};
    EXPECT_EQ(cmd.table, "orders");
    ASSERT_EQ(cmd.values.size(), 2u);
    EXPECT_EQ(cmd.values[0], "1");
}

TEST(Command, VariantHoldsSelectCommand) {
    server::command::Command cmd = server::command::SelectCommand{"t", {}};
    EXPECT_TRUE(std::holds_alternative<server::command::SelectCommand>(cmd));
    EXPECT_FALSE(std::holds_alternative<server::command::InsertCommand>(cmd));
}

TEST(Command, VariantHoldsInsertCommand) {
    server::command::Command cmd = server::command::InsertCommand{"t", {}};
    EXPECT_TRUE(std::holds_alternative<server::command::InsertCommand>(cmd));
    EXPECT_FALSE(std::holds_alternative<server::command::SelectCommand>(cmd));
}

TEST(Result, RowAndResultDefaults) {
    server::result::Row row;
    EXPECT_TRUE(row.cells.empty());

    server::result::Result res;
    EXPECT_FALSE(res.ok);
    EXPECT_TRUE(res.message.empty());
    EXPECT_TRUE(res.rows.empty());
}

// ============================================================================
// pipeline — CRTP contracts via minimal concrete impls
// ============================================================================

namespace {

struct TestAdapter : server::ProtocolAdapterCRTP<TestAdapter> {
    server::ProtocolKind kindImpl() const noexcept { return server::ProtocolKind::tcp; }
    std::string_view nameImpl() const noexcept { return "test"; }
    bool supportsPortImpl(uint16_t port) const noexcept { return port == 8080; }
    bool decodeViewImpl(const ::Event&, std::string_view& out) noexcept {
        out = "decoded";
        return true;
    }
};

struct TestParser : server::ParserCRTP<TestParser> {
    bool parseImpl(std::string_view, server::command::Command& out) noexcept {
        out = server::command::SelectCommand{"t", {}};
        return true;
    }
    std::string_view nameImpl() const noexcept { return "parser"; }
};

struct TestExecutor : server::ExecutorCRTP<TestExecutor> {
    bool executeImpl(const server::command::Command&, server::result::Result& out) noexcept {
        out.ok = true;
        out.message = "ok";
        return true;
    }
    std::string_view nameImpl() const noexcept { return "executor"; }
};

struct TestDistributor : server::DistributorCRTP<TestDistributor> {
    bool distributed = false;
    void distributeImpl(const server::result::Result&) noexcept { distributed = true; }
    std::string_view nameImpl() const noexcept { return "distributor"; }
};

} // namespace

TEST(Pipeline, ProtocolAdapterCRTPDispatch) {
    TestAdapter adapter;
    EXPECT_EQ(adapter.kind(), server::ProtocolKind::tcp);
    EXPECT_EQ(adapter.name(), "test");
    EXPECT_TRUE(adapter.supportsPort(8080));
    EXPECT_FALSE(adapter.supportsPort(9090));

    Event ev;
    std::string_view out;
    EXPECT_TRUE(adapter.decodeView(ev, out));
    EXPECT_EQ(out, "decoded");
}

TEST(Pipeline, ParserCRTPDispatch) {
    TestParser parser;
    EXPECT_EQ(parser.name(), "parser");

    server::command::Command cmd;
    EXPECT_TRUE(parser.parse("input", cmd));
    EXPECT_TRUE(std::holds_alternative<server::command::SelectCommand>(cmd));
}

TEST(Pipeline, ExecutorCRTPDispatch) {
    TestExecutor executor;
    EXPECT_EQ(executor.name(), "executor");

    server::command::Command c+md = server::command::SelectCommand{"t", {}};
    server::result::Result res;
    EXPECT_TRUE(executor.execute(cmd, res));
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.message, "ok");
}

TEST(Pipeline, DistributorCRTPDispatch) {
    TestDistributor dist;
    EXPECT_EQ(dist.name(), "distributor");
    EXPECT_FALSE(dist.distributed);

    server::result::Result res;
    dist.distribute(res);
    EXPECT_TRUE(dist.distributed);
}

TEST(Pipeline, ListenerRuntimeDefaults) {
    server::ListenerRuntime lr;
    EXPECT_EQ(lr.port, 0u);
    EXPECT_EQ(lr.protocol, server::ProtocolKind::custom);
    EXPECT_TRUE(lr.enabled);
}
```

---

### Change 2: `test/CMakeLists.txt` — dodanie nowego pliku testów

**What:** Dodanie `server/server_components_test.cpp` do `main_test`.
**Why:** Nowy plik testów musi być skompilowany.

**Proposed code:**
```cmake
add_executable(main_test
    ...
    server/metrics_mixin_test.cpp
    server/server_components_test.cpp   # <-- dodane
    ...
)
```

#### Review & Status
- [ok ] Awaiting review

---

## Build Errors (if any)

<populated after build attempt>

---

## Test Results (if any)

<populated after test run>
