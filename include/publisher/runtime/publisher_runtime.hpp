//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHER_RUNTIME_HPP
#define MYSERVER_PUBLISHER_RUNTIME_HPP

#include <string_view>

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/resource_store.hpp"
#include "publisher/runtime/sink_traits.hpp"
#include "publisher/runtime/token_registry.hpp"

namespace publisher::runtime
{
    template<publisher::core::SinkKind Sink>
    struct PublisherRuntime;

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::Terminal>
    {
        static void publish_view(TokenRegistry& registry,
                                 OutputResourceStore& store,
                                 publisher::core::PublishToken token,
                                 std::string_view data) noexcept
        {
            const auto idx = registry.resolve(token);
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

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::File>
    {
        static void publish_view(TokenRegistry& registry,
                                 OutputResourceStore& store,
                                 publisher::core::PublishToken token,
                                 std::string_view data) noexcept
        {
            const auto idx = registry.resolve(token);
            auto& handle = store.files[idx];

            SinkTraits<publisher::core::SinkKind::File>::write(handle, data);
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

    template<>
    struct PublisherRuntime<publisher::core::SinkKind::Socket>
    {
        static void publish_view(TokenRegistry& registry,
                                 OutputResourceStore& store,
                                 publisher::core::PublishToken token,
                                 std::string_view data) noexcept
        {
            const auto idx = registry.resolve(token);
            auto& handle = store.sockets[idx];

            SinkTraits<publisher::core::SinkKind::Socket>::write(handle, data);
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
} // namespace publisher::runtime

#endif // MYSERVER_PUBLISHER_RUNTIME_HPP
