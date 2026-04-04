//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHER_TYPES_HPP
#define MYSERVER_PUBLISHER_TYPES_HPP

#include <cstddef>

namespace publisher::core
{
    enum class OutputChannel : std::size_t
    {
        Channel0 = 0,
        Channel1,
        Channel2,
        Channel3,
        Count
    };

    enum class SinkKind : std::size_t
    {
        Terminal = 0,
        File,
        Socket
    };

    [[nodiscard]] constexpr std::size_t toIndex(OutputChannel channel) noexcept
    {
        return static_cast<std::size_t>(channel);
    }

    [[nodiscard]] constexpr const char* toString(OutputChannel channel) noexcept
    {
        switch (channel)
        {
            case OutputChannel::Channel0: return "Channel0";
            case OutputChannel::Channel1: return "Channel1";
            case OutputChannel::Channel2: return "Channel2";
            case OutputChannel::Channel3: return "Channel3";
            default: return "UnknownChannel";
        }
    }

    [[nodiscard]] constexpr const char* toString(SinkKind sink) noexcept
    {
        switch (sink)
        {
            case SinkKind::Terminal: return "Terminal";
            case SinkKind::File:     return "File";
            case SinkKind::Socket:   return "Socket";
            default: return "UnknownSink";
        }
    }
} // namespace publisher::core

#endif // MYSERVER_PUBLISHER_TYPES_HPP
