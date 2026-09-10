#include "server/commands/command.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string_view>

#include "server/commands/handlers.h"
#include "utils/string_utils.h"

namespace redis_simple::command {
namespace {
constexpr auto kUnlimited = std::numeric_limits<size_t>::max();

constexpr CommandArity FixedArity(size_t count) { return {count, count}; }

constexpr CommandArity VariableArity(size_t minimum) {
  return {minimum, kUnlimited};
}

constexpr KeySpec NoKeys() { return {}; }

constexpr KeySpec OneKey() { return {0, 0, 1}; }

constexpr KeySpec AllKeys(size_t first = 0, size_t step = 1) {
  return {first, KeySpec::kAllRemaining, step};
}

constexpr Command ReadCommand(std::string_view name, CommandCallback callback,
                              CommandArity arity, KeySpec keys = NoKeys()) {
  return {name, callback, arity, CommandAccess::kReadOnly, keys};
}

constexpr Command WriteCommand(std::string_view name, CommandCallback callback,
                               CommandArity arity, KeySpec keys = NoKeys()) {
  return {name, callback, arity, CommandAccess::kWrite, keys};
}

constexpr Command AdminCommand(std::string_view name, CommandCallback callback,
                               CommandArity arity) {
  return {name, callback, arity, CommandAccess::kAdmin, NoKeys()};
}

constexpr Command ConnectionCommand(std::string_view name,
                                    CommandCallback callback,
                                    CommandArity arity) {
  return {name, callback, arity, CommandAccess::kConnection, NoKeys()};
}

constexpr char FoldAsciiCase(char value) {
  return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A'))
                                      : value;
}

constexpr int CompareIgnoreCase(std::string_view left, std::string_view right) {
  const size_t length = left.size() < right.size() ? left.size() : right.size();
  for (size_t index = 0; index < length; ++index) {
    const char left_char = FoldAsciiCase(left[index]);
    const char right_char = FoldAsciiCase(right[index]);
    if (left_char != right_char) {
      return left_char < right_char ? -1 : 1;
    }
  }
  if (left.size() == right.size()) {
    return 0;
  }
  return left.size() < right.size() ? -1 : 1;
}

constexpr std::array kCommandTable = {
    WriteCommand("APPEND", strings::HandleAppend, FixedArity(2), OneKey()),
    AdminCommand("BGREWRITEAOF", persistence::HandleBgRewriteAof,
                 FixedArity(0)),
    ReadCommand("DBSIZE", key::HandleDbSize, FixedArity(0)),
    WriteCommand("DECR", strings::HandleDecr, FixedArity(1), OneKey()),
    WriteCommand("DECRBY", strings::HandleDecrBy, FixedArity(2), OneKey()),
    WriteCommand("DEL", key::HandleDel, VariableArity(1), AllKeys()),
    ConnectionCommand("ECHO", session::HandleEcho, FixedArity(1)),
    ReadCommand("EXISTS", key::HandleExists, VariableArity(1), AllKeys()),
    WriteCommand("EXPIRE", key::HandleExpire, FixedArity(2), OneKey()),
    WriteCommand("FLUSHDB", key::HandleFlushDb, FixedArity(0)),
    ReadCommand("GET", strings::HandleGet, FixedArity(1), OneKey()),
    WriteCommand("HDEL", hashes::HandleHDel, VariableArity(2), OneKey()),
    ConnectionCommand("HELLO", session::HandleHello, {0, 1}),
    ReadCommand("HEXISTS", hashes::HandleHExists, FixedArity(2), OneKey()),
    ReadCommand("HGET", hashes::HandleHGet, FixedArity(2), OneKey()),
    ReadCommand("HGETALL", hashes::HandleHGetAll, FixedArity(1), OneKey()),
    WriteCommand("HINCRBY", hashes::HandleHIncrBy, FixedArity(3), OneKey()),
    ReadCommand("HKEYS", hashes::HandleHKeys, FixedArity(1), OneKey()),
    ReadCommand("HLEN", hashes::HandleHLen, FixedArity(1), OneKey()),
    ReadCommand("HMGET", hashes::HandleHMGet, VariableArity(2), OneKey()),
    WriteCommand("HSET", hashes::HandleHSet, VariableArity(3), OneKey()),
    ReadCommand("HVALS", hashes::HandleHVals, FixedArity(1), OneKey()),
    WriteCommand("INCR", strings::HandleIncr, FixedArity(1), OneKey()),
    WriteCommand("INCRBY", strings::HandleIncrBy, FixedArity(2), OneKey()),
    AdminCommand("INFO", persistence::HandleInfo, {0, 1}),
    ReadCommand("LINDEX", lists::HandleLIndex, FixedArity(2), OneKey()),
    ReadCommand("LLEN", lists::HandleLLen, FixedArity(1), OneKey()),
    WriteCommand("LPOP", lists::HandleLPop, FixedArity(1), OneKey()),
    WriteCommand("LPUSH", lists::HandleLPush, VariableArity(2), OneKey()),
    ReadCommand("LRANGE", lists::HandleLRange, FixedArity(3), OneKey()),
    WriteCommand("LREM", lists::HandleLRem, FixedArity(3), OneKey()),
    WriteCommand("LSET", lists::HandleLSet, FixedArity(3), OneKey()),
    WriteCommand("LTRIM", lists::HandleLTrim, FixedArity(3), OneKey()),
    ReadCommand("MGET", strings::HandleMGet, VariableArity(1), AllKeys()),
    WriteCommand("MSET", strings::HandleMSet, VariableArity(2), AllKeys(0, 2)),
    WriteCommand("PERSIST", key::HandlePersist, FixedArity(1), OneKey()),
    WriteCommand("PEXPIRE", key::HandlePExpire, FixedArity(2), OneKey()),
    WriteCommand("PEXPIREAT", key::HandlePExpireAt, FixedArity(2), OneKey()),
    ConnectionCommand("PING", session::HandlePing, {0, 1}),
    ReadCommand("PTTL", key::HandlePTtl, FixedArity(1), OneKey()),
    ConnectionCommand("QUIT", session::HandleQuit, FixedArity(0)),
    WriteCommand("RENAME", key::HandleRename, FixedArity(2), {0, 1, 1}),
    WriteCommand("RPOP", lists::HandleRPop, FixedArity(1), OneKey()),
    WriteCommand("RPUSH", lists::HandleRPush, VariableArity(2), OneKey()),
    WriteCommand("SADD", sets::HandleSAdd, VariableArity(2), OneKey()),
    ReadCommand("SCAN", key::HandleScan, VariableArity(1)),
    ReadCommand("SCARD", sets::HandleSCard, FixedArity(1), OneKey()),
    ReadCommand("SDIFF", sets::HandleSDiff, VariableArity(1), AllKeys()),
    WriteCommand("SET", strings::HandleSet, VariableArity(2), OneKey()),
    ReadCommand("SINTER", sets::HandleSInter, VariableArity(1), AllKeys()),
    ReadCommand("SISMEMBER", sets::HandleSIsMember, FixedArity(2), OneKey()),
    ReadCommand("SMEMBERS", sets::HandleSMembers, FixedArity(1), OneKey()),
    WriteCommand("SREM", sets::HandleSRem, VariableArity(2), OneKey()),
    ReadCommand("STRLEN", strings::HandleStrLen, FixedArity(1), OneKey()),
    ReadCommand("SUNION", sets::HandleSUnion, VariableArity(1), AllKeys()),
    ReadCommand("TTL", key::HandleTtl, FixedArity(1), OneKey()),
    ReadCommand("TYPE", key::HandleType, FixedArity(1), OneKey()),
    WriteCommand("UNLINK", key::HandleUnlink, VariableArity(1), AllKeys()),
    WriteCommand("ZADD", zsets::HandleZAdd, VariableArity(3), OneKey()),
    ReadCommand("ZCARD", zsets::HandleZCard, FixedArity(1), OneKey()),
    ReadCommand("ZCOUNT", zsets::HandleZCount, FixedArity(3), OneKey()),
    ReadCommand("ZRANGE", zsets::HandleZRange, VariableArity(3), OneKey()),
    ReadCommand("ZRANGEBYSCORE", zsets::HandleZRangeByScore, VariableArity(3),
                OneKey()),
    ReadCommand("ZRANK", zsets::HandleZRank, FixedArity(2), OneKey()),
    WriteCommand("ZREM", zsets::HandleZRem, VariableArity(2), OneKey()),
    ReadCommand("ZREVRANGE", zsets::HandleZRevRange, VariableArity(3),
                OneKey()),
    ReadCommand("ZSCORE", zsets::HandleZScore, FixedArity(2), OneKey()),
};

constexpr bool CommandTableIsSorted() {
  for (size_t index = 1; index < kCommandTable.size(); ++index) {
    if (CompareIgnoreCase(kCommandTable[index - 1].name,
                          kCommandTable[index].name) >= 0) {
      return false;
    }
  }
  return true;
}

static_assert(CommandTableIsSorted(), "command table must remain sorted");
}  // namespace

const Command* Find(std::string_view name) {
  const Command* const command =
      std::lower_bound(kCommandTable.begin(), kCommandTable.end(), name,
                       [](const Command& command, std::string_view candidate) {
                         return CompareIgnoreCase(command.name, candidate) < 0;
                       });
  if (command != kCommandTable.end() &&
      utils::EqualsIgnoreCase(name, command->name)) {
    return command;
  }
  return nullptr;
}
}  // namespace redis_simple::command
