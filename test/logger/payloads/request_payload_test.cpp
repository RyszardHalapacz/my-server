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

TEST(RequestPayload, DebugPrintIsCallable) {
  // Compile-time/call check only (CRTP)

  RequestPayload r{};
  // No asserts – must compile and invoke Derived::debug_impl()
 std::ostringstream oss;
r.debug_print(oss);

   std::string out = oss.str();
    std::string expected = "[tag=1] severity=Info timestamp=0 thread_id=0 request_id=0 class_id=0 method_id=0 schema_version=0 req_unique_id=0 path= ";   // pusty, żeby zawsze FAIL i pokazało diff

    EXPECT_EQ(out, expected);

  SUCCEED();
}
