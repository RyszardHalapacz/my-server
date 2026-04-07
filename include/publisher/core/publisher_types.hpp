//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISHER_TYPES_HPP
#define MYSERVER_PUBLISHER_TYPES_HPP

#include <cstddef>

namespace publisher::core
{
    enum class ChannelGroup : std::size_t
    {
        Group0 = 0,
        Group1,
        Group2,
        Group3,
        Count
    };

    enum class ChannelMode : bool
    {
        Exclusive = false,
        Shared    = true
    };

    enum class SinkKind : std::size_t
    {
        Terminal = 0,
        File,
        Socket
    };

    [[nodiscard]] constexpr std::size_t toIndex(ChannelGroup group) noexcept
    {
        return static_cast<std::size_t>(group);
    }

    [[nodiscard]] constexpr const char* toString(ChannelGroup group) noexcept
    {
        switch (group)
        {
            case ChannelGroup::Group0: return "Group0";
            case ChannelGroup::Group1: return "Group1";
            case ChannelGroup::Group2: return "Group2";
            case ChannelGroup::Group3: return "Group3";
            default: return "UnknownGroup";
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
