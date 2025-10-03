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
  // Header occupies 64B, Request fields are located beyond it

  EXPECT_GE(sizeof(RequestPayload), 64u);
#endif
  RequestPayload r{};
  EXPECT_EQ(r.tag(), MsgTag::Request);
}

TEST(RequestPayload, DebugPrintIsCallable) {
  // Compile-time/call check only (CRTP)

  RequestPayload r{};
  r.debug_print();  // No asserts – must compile and invoke Derived::debug_impl()

  SUCCEED();
}
