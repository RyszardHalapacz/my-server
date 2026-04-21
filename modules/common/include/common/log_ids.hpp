#pragma once
#include <cstdint>


enum class LogClassId : uint8_t {
#define X(C) C,
#include "common/log_classes.def"
#undef X
  Count
};


enum class MethodId : uint16_t {
#define X(C,F) C##_##F,
#include "common/log_ids.def"
#undef X
  Count
};
