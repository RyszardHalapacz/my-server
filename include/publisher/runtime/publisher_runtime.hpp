//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHER_RUNTIME_HPP
#define MYSERVER_PUBLISHER_RUNTIME_HPP

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/resource_store.hpp"
#include "publisher/runtime/sink_traits.hpp"
#include "publisher/runtime/token_registry.hpp"

namespace publisher::runtime
{
    // Primary template — intentionally undefined.
    // Passing an unregistered SinkKind is a compile-time error.
    template<publisher::core::SinkKind Sink>
    struct PublisherRuntime;

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::Terminal>
    {
        template<typename Derived>
        static void publish(TokenRegistry& registry,
                            OutputResourceStore& store,
                            publisher::core::PublishToken token,
                            const Derived& obj) noexcept
        {
            const auto channel = registry.resolve(token);
            const auto idx = publisher::core::toIndex(channel);
            auto& handle = store.terminals[idx];

            SinkTraits<publisher::core::SinkKind::Terminal>::write(handle, obj.payload());
        }
    };

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::File>
    {
        template<typename Derived>
        static void publish(TokenRegistry& registry,
                            OutputResourceStore& store,
                            publisher::core::PublishToken token,
                            const Derived& obj) noexcept
        {
            const auto channel = registry.resolve(token);
            const auto idx = publisher::core::toIndex(channel);
            auto& handle = store.files[idx];

            SinkTraits<publisher::core::SinkKind::File>::write(handle, obj.payload());
        }
    };

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::Socket>
    {
        template<typename Derived>
        static void publish(TokenRegistry& registry,
                            OutputResourceStore& store,
                            publisher::core::PublishToken token,
                            const Derived& obj) noexcept
        {
            const auto channel = registry.resolve(token);
            const auto idx = publisher::core::toIndex(channel);
            auto& handle = store.sockets[idx];

            SinkTraits<publisher::core::SinkKind::Socket>::write(handle, obj.payload());
        }
    };
} // namespace publisher::runtime

#endif // MYSERVER_PUBLISHER_RUNTIME_HPP
