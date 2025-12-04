#include <type_traits>
#include "common/messages/traits.hpp"
#include <gtest/gtest.h>

TEST(RequestPayload, IsTriviallyCopyable) {
  static_assert(std::is_trivially_copyable_v<RequestPayload>,
                "Powinno być trivially copyable (zawiera tylko POD + string_view)");
  SUCCEED();
}

TEST(RequestPayload, InheritsHeaderAndAlignment) {
  EXPECT_EQ(alignof(RequestPayload), 64u);

#if MSG_PAD_TO_64B
  // With padding policy enabled, full record should be at least one cache line.
  EXPECT_GE(sizeof(RequestPayload), 64u);
#endif

  RequestPayload r{};
  EXPECT_EQ(r.tag(), MsgTag::Request);
}

TEST(RequestPayload, DebugPrintWithData) {
  RequestPayload r{};
  r.severity = Severity::Error;
  r.timestamp = 1234567890;
  r.thread_id = 42;
  r.request_id = 999;
  r.class_id = 1;
  r.method_id = 2;
  r.schema_version = 1;
  r.req_unique_id = 555;
  r.path = "/api/v1/users";

  std::ostringstream oss;
  r.debug_print(oss);

  std::string out = oss.str();
  std::string expected = 
      "[tag=1] severity=Error timestamp=1234567890 thread_id=42 "
      "request_id=999 class_id=1 method_id=2 schema_version=1 "
      "req_unique_id=555 path=/api/v1/users ";

  EXPECT_EQ(out, expected);
}
