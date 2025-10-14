#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include "common/messages/log_message.hpp"

enum class Severity : std::uint8_t { Info, Warn, Error };

// Set to 1 to pad the header to 64B (cache line).
#ifndef MSG_PAD_TO_64B
#define MSG_PAD_TO_64B 1
#endif

template<MsgTag Tag, typename Derived>
struct alignas(64) PayloadBase {
  static constexpr MsgTag type_id = Tag;

  // — fields (single source of truth via X-macro) —
  #define X(C,F) C F;
  #include "log_payloads.def"
  #undef X

#if MSG_PAD_TO_64B
  // Compute real field size incl. natural intra-struct padding
  struct _FieldsSizeProbe {
    #define X(C,F) C F {};
    #include "log_payloads.def"
    #undef X
  };
  static constexpr std::size_t _fields_size = sizeof(_FieldsSizeProbe);
  static constexpr std::size_t _pad_bytes =
      (_fields_size <= 64) ? (64 - _fields_size) : 0;

  // Zero-size is OK with std::array
  std::array<char, _pad_bytes> _padding{};
  static_assert(_fields_size <= 64, "PayloadBase fields exceed 64 bytes");
  static_assert(_fields_size + _pad_bytes == 64, "Padding math must reach 64B");
#else
  static_assert(alignof(PayloadBase) == 64, "PayloadBase must be 64B-aligned");
#endif

  constexpr MsgTag tag() const { return type_id; }
  void debug_print() const { static_cast<const Derived*>(this)->debug_impl(); }
};
