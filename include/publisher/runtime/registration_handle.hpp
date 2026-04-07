//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_REGISTRATION_HANDLE_HPP
#define MYSERVER_REGISTRATION_HANDLE_HPP

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/token_registry.hpp"

namespace publisher::runtime
{
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
} // namespace publisher::runtime

#endif // MYSERVER_REGISTRATION_HANDLE_HPP
