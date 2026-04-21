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
        static constexpr std::size_t kMaxChannels = 4;
        static constexpr std::size_t kMaxGroups =
            static_cast<std::size_t>(publisher::core::ChannelGroup::Count);
        static constexpr std::size_t kNoChannel = static_cast<std::size_t>(-1);

        TokenRegistry()
        {
            for (std::size_t i = 0; i < kMaxChannels; ++i)
            {
                freeChannels_[i] = i;
            }
            freeChannelCount_ = kMaxChannels;

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
} // namespace publisher::runtime

#endif // MYSERVER_TOKEN_REGISTRY_HPP
