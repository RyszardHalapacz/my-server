//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_TOKEN_REGISTRY_HPP
#define MYSERVER_TOKEN_REGISTRY_HPP

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "publisher/core/publish_token.hpp"
#include "publisher/core/publisher_types.hpp"

namespace publisher::runtime
{
    class TokenRegistry
    {
    public:
        static constexpr std::size_t kMaxBindings = 256;

        [[nodiscard]] publisher::core::PublishToken acquire(publisher::core::OutputChannel channel)
        {
            publisher::core::PublishToken token{};

            if (freeCount_ > 0)
            {
                token = freeTokens_[--freeCount_];
            }
            else
            {
                if (nextToken_ >= kMaxBindings)
                {
                    throw std::runtime_error("TokenRegistry capacity exceeded");
                }

                token = publisher::core::PublishToken{nextToken_++};
            }

            tokenToChannel_[token.value] = channel;
            tokenUsed_[token.value] = true;

            return token;
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

            if (freeCount_ >= kMaxBindings)
            {
                throw std::runtime_error("Free token pool overflow");
            }

            tokenUsed_[token.value] = false;
            freeTokens_[freeCount_++] = token;
        }

        [[nodiscard]] publisher::core::OutputChannel resolve(publisher::core::PublishToken token) const noexcept
        {
            assert(isValidToken(token) && "Invalid publish token");
            assert(tokenUsed_[token.value] && "Inactive publish token");
            return tokenToChannel_[token.value];
        }

    private:
        [[nodiscard]] bool isValidToken(publisher::core::PublishToken token) const noexcept
        {
            return token.value < nextToken_;
        }

    private:
        std::uint32_t nextToken_{0};

        std::array<publisher::core::OutputChannel, kMaxBindings> tokenToChannel_{};
        std::array<bool, kMaxBindings> tokenUsed_{};

        std::array<publisher::core::PublishToken, kMaxBindings> freeTokens_{};
        std::size_t freeCount_{0};
    };
} // namespace publisher::runtime

#endif // MYSERVER_TOKEN_REGISTRY_HPP
