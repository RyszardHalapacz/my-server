//
// Created by RyszardHalapacz on 04/04/2026.
//

#ifndef MYSERVER_PUBLISH_TOKEN_HPP
#define MYSERVER_PUBLISH_TOKEN_HPP

#include <cstdint>

namespace publisher::core {
    struct PublishToken
    {
        std::uint32_t value{};
    };

    inline constexpr PublishToken kInvalidToken{
        static_cast<std::uint32_t>(-1)
    };
}

#endif //MYSERVER_PUBLISH_TOKEN_HPP
