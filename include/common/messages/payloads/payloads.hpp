#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <string_view>
#include <ostream>

#include "common/messages/log_message.hpp"

template<std::size_t N>
struct Padding
{
    std::byte padding[N];
};

// Specialization for N == 0: empty type (no [0]-sized array)
template<>
struct Padding<0>
{
};

enum class Severity : std::uint8_t { Info, Warn, Error };

// Stream output for Severity (used by print_header)
inline std::ostream& operator<<(std::ostream& os, Severity s)
{
    switch (s) {
    case Severity::Info:  return os << "Info";
    case Severity::Warn:  return os << "Warn";
    case Severity::Error: return os << "Error";
    }
    // Fallback for unknown values
    return os << "Unknown(" << static_cast<int>(static_cast<std::uint8_t>(s)) << ')';
}


// Set to 1 to enable padding for Generic header up to 64B.
#ifndef MSG_PAD_TO_64B
#define MSG_PAD_TO_64B 1
#endif

// Lean base: shared header fields + optional padding only for MsgTag::Generic.
template<MsgTag Tag, typename Derived>
struct PayloadBase {
  static constexpr MsgTag type_id = Tag;

  // Header fields: single source of truth via X-macro
  #define X(C,F) C F;
  #include "common/messages/payloads/log_payloads.def"
  #undef X

#if MSG_PAD_TO_64B

  // Probe struct with the SAME layout as the header fields – used only to compute sizeof.
  struct _FieldsSizeProbe {
    #define X(C,F) C F;
    #include "common/messages/payloads/log_payloads.def"
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

  // Padding:
  // - Padding<0> is an empty type; with [[no_unique_address]] it adds no size.
  // - Padding<N> (N > 0) contributes exactly N bytes.
  [[no_unique_address]] Padding<_pad_bytes> _padding{};

  // Extra sanity check only for Generic.
  static_assert(Tag != MsgTag::Generic || _fields_size <= 64u,
                "Generic PayloadBase fields exceed 64 bytes");

#else
  // When global padding is disabled – no trailing bytes.
  static constexpr std::size_t _pad_bytes = 0;
#endif

  constexpr MsgTag tag() const noexcept { return type_id; }

  // Common helper to print header fields for all payloads.
  void print_header(std::ostream& os) const {
    // Tag (you can later map it to a human-readable name)
    os << "[tag=" << static_cast<int>(type_id) << "] ";

    // Print all header fields from the X-macro
    #define X(C,F) os << #F << '=' << F << ' ';
    #include "common/messages/payloads/log_payloads.def"
    #undef X
  }

  // Static polymorphism (CRTP): delegate to Derived::debug_impl(std::ostream&).
  void debug_print(std::ostream& os) const {
    static_cast<const Derived*>(this)->debug_impl(os);
  }
};

// Optional convenience operator<< for any PayloadBase-derived payload.
template<MsgTag Tag, typename Derived>
std::ostream& operator<<(std::ostream& os, const PayloadBase<Tag, Derived>& p) {
  p.debug_print(os);
  return os;
}

