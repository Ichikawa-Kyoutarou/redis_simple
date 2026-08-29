#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace redis_simple::command::strings {
enum class SetCondition : uint8_t {
  kNone,
  kIfMissing,
  kIfExists,
};

struct StringArgs {
  std::string_view key;
  std::string_view value;
  int64_t expire{0};
  int flags{0};
  SetCondition condition{SetCondition::kNone};
  bool return_old_value{};
};
}  // namespace redis_simple::command::strings
