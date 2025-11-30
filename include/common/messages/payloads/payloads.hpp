#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <string_view>

#include "common/messages/log_message.hpp"

enum class Severity : std::uint8_t { Info, Warn, Error };

// Set to 1 to enable padding for Generic header up to 64B.
#ifndef MSG_PAD_TO_64B
#define MSG_PAD_TO_64B 1
#endif

// Lean base: shared header fields + optional padding only for MsgTag::Generic.
template<MsgTag Tag, typename Derived>
struct PayloadBase {
  static constexpr MsgTag type_id = Tag;

  // — fields (single source of truth via X-macro) —
  #define X(C,F) C F;
  #include "common/messages/payloads/log_payloads.def"
  #undef X

#if MSG_PAD_TO_64B

  // Probe struct with THE SAME layout as the header fields – used only to compute sizeof.
  struct _FieldsSizeProbe {
    #define X(C,F) C F;
    #include "common/messages/payloads/log_requestpayload.def"
    #undef X
  };

  static constexpr std::size_t _fields_size = sizeof(_FieldsSizeProbe);

  // Conditional padding:
  // - if Tag == MsgTag::Generic → pad up to 64 bytes (as long as fields < 64),
  // - for all other tags → no padding (0 bytes).
  static constexpr std::size_t _pad_bytes = []() constexpr {
    if constexpr (Tag == MsgTag::Generic) {
      return (_fields_size < 64u) ? (64u - _fields_size) : 0u;
    } else {
      return 0u;
    }
  }();

  // Zero-size std::array is valid, so for _pad_bytes == 0 this does not add any space.
  std::array<char, _pad_bytes> _padding{};

  // Extra sanity check only for Generic (optional but useful).
  static_assert(Tag != MsgTag::Generic || _fields_size <= 64u,
                "Generic PayloadBase fields exceed 64 bytes");

#else
  // When global padding is disabled – no trailing bytes.
  static constexpr std::size_t _pad_bytes = 0;
#endif

  constexpr MsgTag tag() const noexcept { return type_id; }

  // static polymorphism (CRTP)
  void debug_print() const {
    static_cast<const Derived*>(this)->debug_impl();
  }
};
