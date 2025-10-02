#include <array>
#include <string_view>

#include "common/log_ids.hpp"
#include "logger/log_names.hpp" 



inline constexpr auto kClassNames = []{
  std::array<std::string_view, (size_t)LogClassId::Count> a{};
#define X(C) a[(size_t)LogClassId::C] = #C;
#include "common/log_classes.def"
#undef X
  return a;
}();


inline constexpr auto kMethodNames = []{
  std::array<std::string_view, (size_t)MethodId::Count> a{};
  size_t i=0;
#define X(C,F) a[i++] = #F;
#include "common/log_ids.def"
#undef X
  return a;
}();

inline constexpr auto kMethodClass = []{
  std::array<LogClassId, (size_t)MethodId::Count> a{};
  size_t i=0;
#define X(C,F) a[i++] = LogClassId::C;
#include "common/log_ids.def"
#undef X
  return a;
}();



std::string_view className(LogClassId c)   { return kClassNames[(size_t)c]; }
std::string_view methodName(MethodId m)    { return kMethodNames[(size_t)m]; }
LogClassId       methodClass(MethodId m)   { return kMethodClass[(size_t)m]; }
