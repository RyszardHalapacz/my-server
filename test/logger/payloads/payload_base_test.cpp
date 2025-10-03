#include <type_traits>
#include "common/messages/log_message.hpp"
#include "common/messages/payloads/payloads.hpp"
#include <gtest/gtest.h>

struct Dummy {};
using Base = PayloadBase<MsgTag::Request, Dummy>;

TEST(PayloadBase, AlignmentIs64) {
  EXPECT_EQ(alignof(Base), 64u);
}


TEST(PayloadBase, SizePolicy) {
  // "64B alignment is always required.
  EXPECT_EQ(alignof(Base), 64u) << "alignof(Base)=" << alignof(Base);

#if MSG_PAD_TO_64B
 // When cache-line padding is enabled:
// - size should be >= 64 and a multiple of 64
  EXPECT_GE(sizeof(Base), 64u)        << "sizeof(Base)=" << sizeof(Base);
  EXPECT_EQ(sizeof(Base) % 64u, 0u)   << "sizeof(Base)=" << sizeof(Base);
#else
  // Without artificial padding — only a sanity check of the minimal heade
  const size_t min_header =
      sizeof(Severity) + sizeof(std::uint64_t) +
      2 * sizeof(std::uint32_t) + 3 * sizeof(std::uint16_t);
  EXPECT_GE(sizeof(Base), min_header) << "sizeof(Base)=" << sizeof(Base)
                                      << " min_header=" << min_header;
#endif
}

TEST(PayloadBase, DefaultsAreZeroed) {
  Base b{};
  EXPECT_EQ(static_cast<unsigned>(b.severity), 0u);
  EXPECT_EQ(b.timestamp,   0u);
  EXPECT_EQ(b.thread_id,   0u);
  EXPECT_EQ(b.request_id,  0u);
  EXPECT_EQ(b.class_id,    0u);
  EXPECT_EQ(b.method_id,   0u);
  EXPECT_EQ(b.schema_version, 1u);
}

TEST(PayloadBase, TagIsStatic) {
  Base b{};
  EXPECT_EQ(b.tag(), MsgTag::Request);
  EXPECT_EQ(Base::type_id, MsgTag::Request);
}
