#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cli/cli.h"
#include "logging/logger.h"

namespace redis_simple {
namespace {
struct Case {
  std::string command;
  std::string expected_reply;
};

bool ExpectReply(cli::RedisCli* cli, const Case& test_case) {
  cli->AddCommand(test_case.command);
  const std::string reply = cli->ReadReply();
  if (reply != test_case.expected_reply) {
    RS_LOG_DEBUG("command failed: %s expected: %s actual: %s\n",
                 test_case.command.c_str(), test_case.expected_reply.c_str(),
                 reply.c_str());
    return false;
  }
  return true;
}
}  // namespace

int Run() {
  cli::RedisCli cli;
  if (cli.Connect("localhost", 8080) == cli::CliStatus::kError) {
    RS_LOG_DEBUG("failed to connect to integration server\n");
    return EXIT_FAILURE;
  }

  const std::vector<Case> cases = {
      {"SET string_key val PX 1000\r\n", "OK\n"},
      {"GET string_key\r\n", "val\n"},
      {"GET    string_key   \r\n", "val\n"},
      {"GET missing_string_key\r\n", "(nil)\n"},
      {"SET string_key val1 PX 3000\r\n", "OK\n"},
      {"GET string_key\r\n", "val1\n"},
      {"DEL string_key\r\n", "1\n"},
      {"DEL missing_string_key\r\n", "0\n"},
      {"GET string_key\r\n", "(nil)\n"},
      {"SET string_key_1 value1 PX 1000\r\n", "OK\n"},
      {"SET string_key_2 value2 PX 1000\r\n", "OK\n"},
      {"DEL string_key_1 missing_string_key string_key_2\r\n", "2\n"},
      {"GET string_key_1\r\n", "(nil)\n"},
      {"GET string_key_2\r\n", "(nil)\n"},
      {"SET string_ex_key value EX 10\r\n", "OK\n"},
      {"GET string_ex_key\r\n", "value\n"},
      {"SET string_px_key value PX 10000\r\n", "OK\n"},
      {"GET string_px_key\r\n", "value\n"},
      {"SET string_exat_key value EXAT 4102444800\r\n", "OK\n"},
      {"GET string_exat_key\r\n", "value\n"},
      {"SET string_pxat_key value PXAT 4102444800000\r\n", "OK\n"},
      {"GET string_pxat_key\r\n", "value\n"},
      {"SET string_past_expiration value PXAT 1\r\n", "OK\n"},
      {"GET string_past_expiration\r\n", "(nil)\n"},
      {"SET string_lowercase_option value px 10000\r\n", "OK\n"},
      {"GET string_lowercase_option\r\n", "value\n"},
      {"SET string_nx_key first NX\r\n", "OK\n"},
      {"SET string_nx_key second NX\r\n", "(nil)\n"},
      {"GET string_nx_key\r\n", "first\n"},
      {"SET string_xx_missing value XX\r\n", "(nil)\n"},
      {"GET string_xx_missing\r\n", "(nil)\n"},
      {"SET string_xx_key first\r\n", "OK\n"},
      {"SET string_xx_key second XX\r\n", "OK\n"},
      {"GET string_xx_key\r\n", "second\n"},
      {"SET string_get_key first\r\n", "OK\n"},
      {"SET string_get_key second GET\r\n", "first\n"},
      {"GET string_get_key\r\n", "second\n"},
      {"SET string_get_missing first GET\r\n", "(nil)\n"},
      {"GET string_get_missing\r\n", "first\n"},
      {"SET string_get_nx existing\r\n", "OK\n"},
      {"SET string_get_nx ignored GET NX\r\n", "existing\n"},
      {"GET string_get_nx\r\n", "existing\n"},
      {"SET string_get_xx_missing ignored XX GET\r\n", "(nil)\n"},
      {"GET string_get_xx_missing\r\n", "(nil)\n"},
      {"SET string_conflict original\r\n", "OK\n"},
      {"SET string_conflict changed NX XX\r\n", "ERR syntax error\n"},
      {"GET string_conflict\r\n", "original\n"},
      {"SET string_conflict changed GET GET\r\n", "ERR syntax error\n"},
      {"GET string_conflict\r\n", "original\n"},
      {"SET string_conflict changed EXAT 4102444800 PX 1000\r\n",
       "ERR syntax error\n"},
      {"GET string_conflict\r\n", "original\n"},
      {"SET string_invalid_exat value EXAT 9223372036854775807\r\n",
       "ERR syntax error\n"},
      {"SADD string_get_wrong_type member\r\n", "1\n"},
      {"SET string_get_wrong_type value NX GET\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
      {"TYPE string_get_wrong_type\r\n", "set\n"},
      {"INCR string_counter\r\n", "1\n"},
      {"INCR string_counter\r\n", "2\n"},
      {"DECR string_counter\r\n", "1\n"},
      {"INCRBY string_counter 10\r\n", "11\n"},
      {"DECRBY string_counter 3\r\n", "8\n"},
      {"INCRBY string_counter -10\r\n", "-2\n"},
      {"DECRBY string_counter -3\r\n", "1\n"},
      {"INCRBY string_incrby_missing 5\r\n", "5\n"},
      {"DECRBY string_decrby_missing 2\r\n", "-2\n"},
      {"SET string_max_integer 9223372036854775807\r\n", "OK\n"},
      {"INCRBY string_max_integer 1\r\n",
       "ERR increment or decrement would overflow\n"},
      {"GET string_max_integer\r\n", "9223372036854775807\n"},
      {"SET string_min_integer -9223372036854775808\r\n", "OK\n"},
      {"DECRBY string_min_integer 1\r\n",
       "ERR increment or decrement would overflow\n"},
      {"GET string_min_integer\r\n", "-9223372036854775808\n"},
      {"SET string_decrby_min -1\r\n", "OK\n"},
      {"DECRBY string_decrby_min -9223372036854775808\r\n",
       "9223372036854775807\n"},
      {"INCRBY string_counter invalid\r\n", "ERR value is not an integer\n"},
      {"APPEND string_append hello\r\n", "5\n"},
      {"APPEND string_append _world\r\n", "11\n"},
      {"GET string_append\r\n", "hello_world\n"},
      {"STRLEN string_append\r\n", "11\n"},
      {"STRLEN string_strlen_missing\r\n", "0\n"},
      {"MSET string_mget_1 one string_mget_2 two\r\n", "OK\n"},
      {"MGET string_mget_1 missing_string_key string_mget_2\r\n",
       "one\n(nil)\ntwo\n\n\n"},
      {"RPUSH string_wrong_type item\r\n", "1\n"},
      {"GET string_wrong_type\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
      {"INCR string_wrong_type\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
      {"INCRBY string_wrong_type 1\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
      {"DECRBY string_wrong_type 1\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
      {"STRLEN string_wrong_type\r\n",
       "WRONGTYPE Operation against a key holding the wrong kind of value\n"},
      {"DEL\r\n", "ERR wrong number of arguments\n"},
  };
  for (const Case& test_case : cases) {
    if (!ExpectReply(&cli, test_case)) {
      return EXIT_FAILURE;
    }
  }
  cli.AddCommand(std::vector<std::string_view>{"SET", "key with spaces",
                                               "value with spaces"});
  if (cli.ReadReply() != "OK\n") {
    return EXIT_FAILURE;
  }
  cli.AddCommand(std::vector<std::string_view>{"GET", "key with spaces"});
  if (cli.ReadReply() != "value with spaces\n") {
    return EXIT_FAILURE;
  }
  cli.AddCommand(std::vector<std::string_view>{"SET", "binary strlen",
                                               std::string_view("a\0b", 3)});
  if (cli.ReadReply() != "OK\n") {
    return EXIT_FAILURE;
  }
  cli.AddCommand(std::vector<std::string_view>{"STRLEN", "binary strlen"});
  if (cli.ReadReply() != "3\n") {
    return EXIT_FAILURE;
  }
  cli.AddCommand(std::vector<std::string_view>{"HELLO", "3"});
  if (cli.ReadReply().find("proto\n3\n") == std::string::npos) {
    return EXIT_FAILURE;
  }
  if (!ExpectReply(&cli, {"SET string_resp3 value NX\r\n", "OK\n"}) ||
      !ExpectReply(&cli, {"SET string_resp3 ignored NX\r\n", "(nil)\n"}) ||
      !ExpectReply(
          &cli, {"SET string_resp3_missing ignored XX GET\r\n", "(nil)\n"})) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
}  // namespace redis_simple

int main() {
  try {
    return redis_simple::Run();
  } catch (...) {
    return EXIT_FAILURE;
  }
}
