//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHABLE_HPP
#define MYSERVER_PUBLISHABLE_HPP

#include <utility>

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/registration_handle.hpp"
#include "publisher/runtime/resource_store.hpp"
#include "publisher/runtime/token_registry.hpp"
#include "publisher/runtime/publisher_runtime.hpp"

namespace publisher::api
{
    template<typename Derived>
    struct Publishable
    {
        explicit Publishable(publisher::runtime::RegistrationHandle handle)
            : handle_(std::move(handle))
        {}

        Publishable(const Publishable&) = delete;
        Publishable& operator=(const Publishable&) = delete;
        Publishable(Publishable&&) noexcept = default;
        Publishable& operator=(Publishable&&) noexcept = default;
        ~Publishable() = default;

        template<publisher::core::SinkKind Sink>
        void publish(publisher::runtime::TokenRegistry& tokenRegistry,
                     publisher::runtime::OutputResourceStore& resourceStore) const
        {
            const auto& self = static_cast<const Derived&>(*this);
            publisher::runtime::PublisherRuntime<Sink>::publish(
                tokenRegistry,
                resourceStore,
                handle_.token(),
                self
            );
        }

        [[nodiscard]] publisher::core::PublishToken publishToken() const noexcept
        {
            return handle_.token();
        }

    private:
        publisher::runtime::RegistrationHandle handle_;
    };
} // namespace publisher::api

#endif // MYSERVER_PUBLISHABLE_HPP