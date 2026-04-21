//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_SINK_TRAITS_HPP
#define MYSERVER_SINK_TRAITS_HPP

#include <cassert>
#include <string_view>

#include "publisher/core/publisher_types.hpp"
#include "publisher/runtime/sink_handles.hpp"

namespace publisher::runtime
{
    template<publisher::core::SinkKind Sink>
    struct SinkTraits;

    template<>
    struct SinkTraits<publisher::core::SinkKind::Terminal>
    {
        using handle_type = TerminalHandle;

        static void write(handle_type& handle, std::string_view data) noexcept
        {
            assert(handle.out != nullptr && "TerminalHandle: null stream");
            handle.out->write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    };

    template<>
    struct SinkTraits<publisher::core::SinkKind::File>
    {
        using handle_type = FileHandle;

        static void write(handle_type& handle, std::string_view data) noexcept
        {
            assert(handle.file != nullptr && "FileHandle: null stream");
            handle.file->write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    };

    template<>
    struct SinkTraits<publisher::core::SinkKind::Socket>
    {
        using handle_type = SocketHandle;

        static void write(handle_type& handle, std::string_view data) noexcept
        {
            assert(handle.fakeFd >= 0 && "SocketHandle: invalid fd");
            (void)handle;
            (void)data;
            // TODO: real socket write
        }
    };
} // namespace publisher::runtime

#endif // MYSERVER_SINK_TRAITS_HPP
