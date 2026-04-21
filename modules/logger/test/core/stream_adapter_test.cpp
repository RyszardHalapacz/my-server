#include <gtest/gtest.h>
#include <string>
#include <ostream>

#include "logger/core/stream_adapter.hpp"
#include "common/messages/payloads/payloads.hpp"

using logger::core::detail::StreamAdapter;
using logger::core::detail::EnvelopeLike;

// For readability: real request payload type from your project
using Env = RequestPayload;

// ------------------------------------------------------------------
// DummyPayload – derived from PayloadBase to prove that any
// PayloadBase-derived type satisfying the contract works with
// StreamAdapter and EnvelopeLike.
// ------------------------------------------------------------------

struct DummyPayload : PayloadBase<MsgTag::Request, DummyPayload> {
  // Minimal implementation required by PayloadBase::debug_print(...)
  // PayloadBase::debug_print(os) will delegate to Derived::debug_impl(os).
  void debug_impl(std::ostream& os) const {
    os << "DummyPayloadDebug";
  }
};

// Non-envelope type: missing print_header(), should be rejected
struct WithoutPrintHeader {
  void debug_print(std::ostream& os) const {
    os << "should_not_be_envelope_like";
  }
};

// ------------------------------------------------------------------
// Concept sanity checks
// ------------------------------------------------------------------

TEST(StreamAdapter, RequestPayloadIsEnvelopeLike) {
  static_assert(EnvelopeLike<Env>);
}

TEST(StreamAdapter, DummyPayloadDerivedFromPayloadBaseIsEnvelopeLike) {
  static_assert(EnvelopeLike<DummyPayload>);
}

TEST(StreamAdapter, RejectsTypesWithoutPrintHeader) {
  static_assert(!EnvelopeLike<WithoutPrintHeader>);
  static_assert(!EnvelopeLike<int>);
}

// ------------------------------------------------------------------
// to_string() behavior for real RequestPayload
// ------------------------------------------------------------------

TEST(StreamAdapter, UsesDebugPrintForRequestPayload) {
  Env r{};
  r.severity       = Severity::Error;
  r.timestamp      = 1234567890;
  r.thread_id      = 42;
  r.request_id     = 999;
  r.class_id       = 1;
  r.method_id      = 2;
  r.schema_version = 1;
  r.req_unique_id  = 555;
  r.path           = "/api/v1/users";

  // StreamAdapter should internally call r.debug_print(ostream&)
  std::string out = StreamAdapter::to_string(r);

  std::string expected =
      "[tag=1] severity=Error timestamp=1234567890 thread_id=42 "
      "request_id=999 class_id=1 method_id=2 schema_version=1 "
      "req_unique_id=555 path=/api/v1/users ";

  EXPECT_EQ(out, expected);
}

// ------------------------------------------------------------------
// to_string() behavior for DummyPayload
// ------------------------------------------------------------------

TEST(StreamAdapter, WorksForAnyPayloadBaseDerivedType) {
  DummyPayload d{};

  std::string out = StreamAdapter::to_string(d);

  // We only verify that:
  //  - the code compiles (EnvelopeLike satisfied),
  //  - to_string() produces a non-empty string with our debug marker.
  EXPECT_FALSE(out.empty());
  EXPECT_NE(out.find("DummyPayloadDebug"), std::string::npos);
}
