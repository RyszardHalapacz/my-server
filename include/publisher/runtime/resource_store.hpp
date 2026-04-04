//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_RESOURCE_STORE_HPP
#define MYSERVER_RESOURCE_STORE_HPP

#include <array>
#include <cstddef>

#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/sink_handles.hpp"

namespace publisher::runtime
{
    struct OutputResourceStore
    {
        static constexpr std::size_t kChannelCount =
            static_cast<std::size_t>(publisher::core::OutputChannel::Count);

        std::array<TerminalHandle, kChannelCount> terminals{};
        std::array<FileHandle,     kChannelCount> files{};
        std::array<SocketHandle,   kChannelCount> sockets{};
    };
} // namespace publisher::runtime

#endif // MYSERVER_RESOURCE_STORE_HPP
