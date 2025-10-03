#pragma once
#include <string_view>
#include "common/messages/payloads/payloads.hpp"

struct RequestPayload : PayloadBase<MsgTag::Request, RequestPayload> {
  std::uint64_t  req_unique_id{};
  std::string_view path{};

  void debug_impl() const {
    // log/printf fields; leave empty if not needed.
  }
};
