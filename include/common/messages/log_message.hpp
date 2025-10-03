#pragma once
#include <cstdint>


enum class MsgTag  : std::uint8_t {
#define X(M) M,
#include "common/messages/log_message.def"
#undef X
  Count
};
