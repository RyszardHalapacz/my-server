// include/logger/registry/header_args.hpp
#pragma once

#include <tuple>
#include <utility>

namespace logger::registry {

inline constexpr auto pack_header_args =
    []<typename... Args>(Args&&... args) {
        return std::tuple<std::decay_t<Args>...>{
            std::forward<Args>(args)...
        };
    };
} // namespace logger::registry
