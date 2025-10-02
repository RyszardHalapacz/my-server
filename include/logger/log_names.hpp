#pragma once
#include <string_view>
#include "common/log_ids.hpp"

std::string_view className(LogClassId);
std::string_view methodName(MethodId);
LogClassId       methodClass(MethodId);
