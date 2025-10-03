#pragma once
#include <cstdint>
#include "common/messages/log_message.hpp"

enum class Severity : std::uint8_t { Info, Warn, Error };

// Set to 1 to pad the header to 64B (cache line).
#ifndef MSG_PAD_TO_64B
#define MSG_PAD_TO_64B 1
#endif

template<MsgTag Tag, typename Derived>
struct alignas(64) PayloadBase {
  static constexpr MsgTag type_id = Tag;

  Severity       severity{};        // info/warn/error
  std::uint64_t  timestamp{};       // monotonic time ticks
  std::uint32_t  thread_id{};       // mapped thread id
  std::uint32_t  request_id{};      // correlation id
  std::uint16_t  class_id{};        // X-macro class id
  std::uint16_t  method_id{};       // X-macro method id
  std::uint16_t  schema_version{1}; // ABI/schema version

#if MSG_PAD_TO_64B
  char _padding[64 - ( sizeof(Severity)
                     + sizeof(std::uint64_t)
                     + sizeof(std::uint32_t)*2
                     + sizeof(std::uint16_t)*3 )] = {};
#endif

  constexpr MsgTag tag() const { return type_id; }

  // static polymorphism
  void debug_print() const { static_cast<const Derived*>(this)->debug_impl(); }
};
