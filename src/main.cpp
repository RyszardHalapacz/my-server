#include "server/server.h"
#include "event.hpp"
#include "logger/logger.hpp"
#include "logger/registry/builder.hpp"
#include "logger/registry/header_args.hpp"
#include "logger/core/stream_adapter.hpp"
#include "publisher/runtime/publisher_runtime.hpp"
#include "publisher/runtime/token_registry.hpp"
#include "publisher/runtime/resource_store.hpp"
#include "publisher/runtime/registration_handle.hpp"

using namespace publisher::runtime;
using namespace publisher::core;
using logger::Handler;

int main()
{

    // --- nowy token-based runtime publisher ---
    TokenRegistry        registry;
    OutputResourceStore  store;

    for (std::size_t i = 0; i < OutputResourceStore::kChannelCount; ++i)
        store.terminals[i].out = &std::cout;

    RegistrationHandle   handle(registry);

    // 1. Zbuduj payload (te same narzędzia co Handler::log wewnętrznie)
    auto args = logger::registry::pack_header_args(
        Severity::Error,
        /*timestamp*/    std::uint64_t{999999},
        /*thread_id*/    std::uint32_t{1},
        /*request_id*/   std::uint32_t{42},
        /*class_id*/     std::uint16_t{1},
        /*method_id*/    std::uint16_t{2},
        /*schema_ver*/   std::uint16_t{1},
        /*req_unique_id*/std::uint64_t{100},
        /*path*/         std::string_view{"/api/hello"}
    );
    auto payload = logger::registry::Builder::build<MsgTag::Request>(std::move(args));

    // 2. Sformatuj do string_view — zero alokacji (stack buffer)
    logger::core::detail::FixedStringBuf<1024> buf;
    std::ostream os(&buf);
    payload.debug_print(os);

    // 3. Publish przez runtime publisher
    PublisherRuntime<SinkKind::Terminal>::publish_view(
        registry, store, handle.token(), buf.view()
    );
}
