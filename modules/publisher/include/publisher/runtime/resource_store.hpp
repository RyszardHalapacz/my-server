//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_RESOURCE_STORE_HPP
#define MYSERVER_RESOURCE_STORE_HPP

#include <array>
#include <cstddef>

#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/sink_handles.hpp"
#include "publisher/runtime/token_registry.hpp"

namespace publisher::runtime
{
    struct OutputResourceStore
    {
        static constexpr std::size_t kChannelCount = TokenRegistry::kMaxChannels;

        std::array<TerminalHandle, kChannelCount> terminals{};
        std::array<FileHandle,     kChannelCount> files{};
        std::array<SocketHandle,   kChannelCount> sockets{};
    };
} // namespace publisher::runtime

#endif // MYSERVER_RESOURCE_STORE_HPP
