#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "cli/cli.h"
#include "logging/logger.h"

namespace redis_simple {
namespace {
constexpr int kAofPort = 18081;

struct Case {
  std::string command;
  std::string expected_reply;
};

bool ExpectReply(cli::RedisCli* const cli, const Case& test_case) {
  cli->AddCommand(test_case.command);
  const std::string reply = cli->ReadReply();
  if (reply == test_case.expected_reply) {
    return true;
  }
  RS_LOG_DEBUG("AOF command failed: %s expected: %s actual: %s\n",
               test_case.command.c_str(), test_case.expected_reply.c_str(),
               reply.c_str());
  return false;
}

std::vector<std::string> NonEmptyLines(const std::string& reply) {
  std::stringstream stream(reply);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(stream, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

bool WriteDataset(cli::RedisCli* const cli) {
  const std::vector<Case> cases = {
      {"SET aof_flushed gone\r\n", "OK\n"},
      {"FLUSHDB\r\n", "OK\n"},
      {"SET aof_string value\r\n", "OK\n"},
      {"SET aof_conditional initial NX\r\n", "OK\n"},
      {"SET aof_conditional ignored NX\r\n", "(nil)\n"},
      {"SET aof_conditional updated XX GET\r\n", "initial\n"},
      {"SET aof_ttl live PX 60000\r\n", "OK\n"},
      {"SET aof_persist durable PX 60000\r\n", "OK\n"},
      {"PERSIST aof_persist\r\n", "1\n"},
      {"SET aof_expired old PX 100\r\n", "OK\n"},
      {"APPEND aof_expired _suffix\r\n", "10\n"},
      {"HSET aof_hash field value\r\n", "1\n"},
      {"RPUSH aof_list one two\r\n", "2\n"},
      {"SADD aof_set first second\r\n", "2\n"},
      {"ZADD aof_zset 1 member 3 removed\r\n", "2\n"},
      {"MSET aof_mset_one one aof_mset_two two\r\n", "OK\n"},
      {"SET aof_rename_source renamed\r\n", "OK\n"},
      {"INCR aof_counter\r\n", "1\n"},
      {"INCR aof_counter\r\n", "2\n"},
      {"SET aof_deleted gone\r\n", "OK\n"},
      {"DEL aof_deleted\r\n", "1\n"},
      {"HSET aof_string field rejected\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
  };
  if (!std::all_of(cases.begin(), cases.end(), [cli](const auto& test_case) {
        return ExpectReply(cli, test_case);
      })) {
    return false;
  }
  for (int length = 1; length <= 256; ++length) {
    if (!ExpectReply(
            cli, {"APPEND aof_history x\r\n", std::to_string(length) + "\n"})) {
      return false;
    }
  }
  return true;
}

bool RewriteDataset(cli::RedisCli* const cli) {
  const std::vector<Case> cases = {
      {"BGREWRITEAOF\r\n", "Background append only file rewriting started\n"},
      {"SET aof_string updated\r\n", "OK\n"},
      {"HSET aof_hash second added\r\n", "1\n"},
      {"HDEL aof_hash field\r\n", "1\n"},
      {"RPUSH aof_list three\r\n", "3\n"},
      {"LTRIM aof_list 1 -1\r\n", "OK\n"},
      {"LREM aof_list 1 two\r\n", "1\n"},
      {"SREM aof_set second\r\n", "1\n"},
      {"ZADD aof_zset 2 member\r\n", "0\n"},
      {"ZREM aof_zset removed\r\n", "1\n"},
      {"MSET aof_mset_one updated aof_mset_two changed\r\n", "OK\n"},
      {"RENAME aof_rename_source aof_rename_target\r\n", "OK\n"},
  };
  if (!std::all_of(cases.begin(), cases.end(), [cli](const auto& test_case) {
        return ExpectReply(cli, test_case);
      })) {
    return false;
  }
  cli->AddCommand("INFO persistence\r\n");
  const std::string info = cli->ReadReply();
  return info.find("aof_enabled:1") != std::string::npos &&
         info.find("aof_current_size:") != std::string::npos;
}

bool ReadDataset(cli::RedisCli* const cli) {
  const std::vector<Case> cases = {
      {"GET aof_string\r\n", "updated\n"},
      {"GET aof_conditional\r\n", "updated\n"},
      {"GET aof_expired\r\n", "(nil)\n"},
      {"GET aof_flushed\r\n", "(nil)\n"},
      {"GET aof_persist\r\n", "durable\n"},
      {"PTTL aof_persist\r\n", "-1\n"},
      {"HGET aof_hash field\r\n", "(nil)\n"},
      {"HGET aof_hash second\r\n", "added\n"},
      {"SISMEMBER aof_set first\r\n", "1\n"},
      {"SISMEMBER aof_set second\r\n", "0\n"},
      {"ZSCORE aof_zset member\r\n", "2\n"},
      {"ZSCORE aof_zset removed\r\n", "(nil)\n"},
      {"GET aof_mset_one\r\n", "updated\n"},
      {"GET aof_mset_two\r\n", "changed\n"},
      {"GET aof_rename_source\r\n", "(nil)\n"},
      {"GET aof_rename_target\r\n", "renamed\n"},
      {"GET aof_counter\r\n", "2\n"},
      {"GET aof_deleted\r\n", "(nil)\n"},
      {"GET aof_history\r\n", std::string(256, 'x') + "\n"},
  };
  if (!std::all_of(cases.begin(), cases.end(), [cli](const auto& test_case) {
        return ExpectReply(cli, test_case);
      })) {
    return false;
  }

  cli->AddCommand("LRANGE aof_list 0 -1\r\n");
  if (NonEmptyLines(cli->ReadReply()) != std::vector<std::string>({"three"})) {
    return false;
  }

  cli->AddCommand("PTTL aof_ttl\r\n");
  const std::string ttl_reply = cli->ReadReply();
  int64_t ttl = 0;
  const auto parsed = std::from_chars(ttl_reply.data(),
                                      ttl_reply.data() + ttl_reply.size(), ttl);
  return parsed.ec == std::errc() && ttl > 0 && ttl <= 60'000;
}

bool WriteAutoRewriteDataset(cli::RedisCli* const cli) {
  if (!ExpectReply(cli, {"SET aof_auto 0\r\n", "OK\n"})) {
    return false;
  }
  for (int length = 2; length <= 129; ++length) {
    if (!ExpectReply(
            cli, {"APPEND aof_auto x\r\n", std::to_string(length) + "\n"})) {
      return false;
    }
  }

  constexpr int kMaxAttempts = 500;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    cli->AddCommand("INFO persistence\r\n");
    const std::string info = cli->ReadReply();
    if (info.find("aof_rewrite_in_progress:0") != std::string::npos &&
        info.find("aof_last_bgrewrite_status:ok") != std::string::npos) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

bool ReadAutoRewriteDataset(cli::RedisCli* const cli) {
  return ExpectReply(cli,
                     {"GET aof_auto\r\n", "0" + std::string(128, 'x') + "\n"});
}
}  // namespace

int Run(std::string_view mode) {
  cli::RedisCli cli;
  if (cli.Connect("127.0.0.1", kAofPort) == cli::CliStatus::kError) {
    return EXIT_FAILURE;
  }
  if (mode == "write") {
    return WriteDataset(&cli) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (mode == "read") {
    return ReadDataset(&cli) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (mode == "rewrite") {
    return RewriteDataset(&cli) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (mode == "auto-write") {
    return WriteAutoRewriteDataset(&cli) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (mode == "auto-read") {
    return ReadAutoRewriteDataset(&cli) ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  return EXIT_FAILURE;
}
}  // namespace redis_simple

int main(int argc, char* argv[]) {
  try {
    return argc == 2 ? redis_simple::Run(argv[1]) : EXIT_FAILURE;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
