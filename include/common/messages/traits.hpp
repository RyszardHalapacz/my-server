#pragma once
#include <string_view>
#include "common/messages/payloads/payloads.hpp"

struct alignas(64) RequestPayload  : PayloadBase<MsgTag::Request, RequestPayload> {

    // — fields (single source of truth via X-macro) —
  #define X(C,F) C F;
  #include "payloads/log_requestpayload.def"
  #undef X


  void debug_impl() const {
    // log/printf fields; leave empty if not needed.
  }
};
