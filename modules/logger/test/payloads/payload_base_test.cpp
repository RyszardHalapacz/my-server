#include <type_traits>
#include "common/messages/log_message.hpp"
#include "common/messages/payloads/payloads.hpp"
#include <gtest/gtest.h>

struct Dummy {};
using Base = PayloadBase<MsgTag::Request, Dummy>;

// PayloadBase is a lean header now – no 64B alignment requirement anymore.
TEST(PayloadBase, SizeAtLeastHeaderFields) {
  // Probe with the SAME layout as the header fields from "common/messages/payloads/log_requestpayload.def"
  struct HeaderProbe {
  #define X(C,F) C F;
  #include "common/messages/payloads/log_payloads.def"
  #undef X
  };

  constexpr std::size_t fields_size = sizeof(HeaderProbe);

  // Base must be at least as big as the header fields (may be bigger due to natural padding)
  EXPECT_GE(sizeof(Base), fields_size)
      << "sizeof(Base)=" << sizeof(Base)
      << " fields_size=" << fields_size;
}

TEST(PayloadBase, DefaultsMatchDefinition) {
  Base b{};                       

  // Probe with the SAME layout and initializers as in .def
  struct DefaultsProbe {
  #define X(C,F) C F;
  #include "common/messages/payloads/log_payloads.def"
  #undef X
  } def{};

  // Compare each field with the probe – works also for non-zero defaults
  #define X(C,F) EXPECT_EQ(b.F, def.F);
  #include "common/messages/payloads/log_payloads.def"
  #undef X
}

TEST(PayloadBase, TagIsStatic) {
  Base b{};
  EXPECT_EQ(b.tag(), MsgTag::Request);
  EXPECT_EQ(Base::type_id, MsgTag::Request);
}
