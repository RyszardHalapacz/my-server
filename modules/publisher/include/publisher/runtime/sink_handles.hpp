//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_SINK_HANDLES_HPP
#define MYSERVER_SINK_HANDLES_HPP

#include <fstream>
#include <iosfwd>

namespace publisher::runtime {

    struct TerminalHandle
    {
        std::ostream* out{};
    };

    struct FileHandle
    {
        std::fstream* file{};
    };

    struct SocketHandle
    {
        int fakeFd{-1};
    };
}
#endif //MYSERVER_SINK_HANDLES_HPP
