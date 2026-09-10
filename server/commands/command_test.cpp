#include "server/commands/command.h"

#include <gtest/gtest.h>

#include <array>

namespace redis_simple::command {
TEST(CommandRegistryTest, FindsCommandsCaseInsensitively) {
  const auto* get = Find("get");
  ASSERT_NE(get, nullptr);
  EXPECT_EQ(get->name, "GET");
  EXPECT_NE(get->callback, nullptr);

  const auto* zrange = Find("ZRANGE");
  ASSERT_NE(zrange, nullptr);
  EXPECT_EQ(zrange->name, "ZRANGE");
  EXPECT_NE(zrange->callback, nullptr);

  const auto* hset = Find("hset");
  ASSERT_NE(hset, nullptr);
  EXPECT_EQ(hset->name, "HSET");
  EXPECT_NE(hset->callback, nullptr);
}

TEST(CommandRegistryTest, FindsExpandedRedisCommandSet) {
  const std::array names = {
      "EXPIRE",    "PEXPIRE",       "PEXPIREAT", "TTL",     "PTTL",
      "PERSIST",   "RENAME",        "DBSIZE",    "FLUSHDB", "INCR",
      "DECR",      "DECRBY",        "APPEND",    "MGET",    "MSET",
      "STRLEN",    "INCRBY",        "LINDEX",    "LSET",    "LREM",
      "LTRIM",     "SINTER",        "SUNION",    "SDIFF",   "ZCOUNT",
      "ZREVRANGE", "ZRANGEBYSCORE", "HMGET",     "HKEYS",   "HVALS",
      "HINCRBY",   "HELLO",         "PING",      "ECHO",    "QUIT",
      "SCAN",      "BGREWRITEAOF",  "INFO"};
  for (const auto* name : names) {
    const auto* command = Find(name);
    ASSERT_NE(command, nullptr) << name;
    EXPECT_NE(command->callback, nullptr);
  }
}

TEST(CommandRegistryTest, ReturnsNullForUnknownCommand) {
  EXPECT_EQ(Find("UNKNOWN"), nullptr);
  EXPECT_EQ(Find(""), nullptr);
}

TEST(CommandRegistryTest, ExposesCommandMetadata) {
  const auto* get = Find("GET");
  ASSERT_NE(get, nullptr);
  EXPECT_TRUE(get->arity.Accepts(1));
  EXPECT_FALSE(get->arity.Accepts(0));
  EXPECT_EQ(get->access, CommandAccess::kReadOnly);
  EXPECT_TRUE(get->keys.HasKeys());
  EXPECT_EQ(get->keys.first, 0);
  EXPECT_EQ(get->keys.last, 0);

  const auto* mset = Find("MSET");
  ASSERT_NE(mset, nullptr);
  EXPECT_EQ(mset->access, CommandAccess::kWrite);
  EXPECT_EQ(mset->keys.first, 0);
  EXPECT_EQ(mset->keys.last, KeySpec::kAllRemaining);
  EXPECT_EQ(mset->keys.step, 2);

  const auto* ping = Find("PING");
  ASSERT_NE(ping, nullptr);
  EXPECT_EQ(ping->access, CommandAccess::kConnection);
  EXPECT_FALSE(ping->keys.HasKeys());

  const auto* rewrite = Find("BGREWRITEAOF");
  ASSERT_NE(rewrite, nullptr);
  EXPECT_EQ(rewrite->access, CommandAccess::kAdmin);
  EXPECT_FALSE(rewrite->keys.HasKeys());

  const auto* info = Find("INFO");
  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->access, CommandAccess::kAdmin);
  EXPECT_TRUE(info->arity.Accepts(0));
  EXPECT_TRUE(info->arity.Accepts(1));
}
}  // namespace redis_simple::command
