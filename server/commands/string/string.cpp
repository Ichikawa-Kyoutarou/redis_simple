#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "server/client.h"
#include "server/commands/handlers.h"
#include "server/db/db.h"
#include "server/reply.h"
#include "utils/string_utils.h"

namespace redis_simple::command::strings {
namespace {
enum class StringStatus : std::uint8_t {
  kOk,
  kMissing,
  kWrongType,
  kError,
};

enum class IntegerOperation : std::uint8_t {
  kAdd,
  kSubtract,
};

struct StringResult {
  const std::string* value;
  StringStatus status;
};

StringResult LookupString(db::RedisDb* const redis_db, std::string_view key) {
  const auto* object = redis_db->LookupKey(key);
  if (object == nullptr) {
    return {nullptr, StringStatus::kMissing};
  }
  if (object->Type() != db::RedisObject::ObjectType::kString) {
    return {nullptr, StringStatus::kWrongType};
  }
  return {&object->String(), StringStatus::kOk};
}

std::optional<int64_t> ToReplyInteger(size_t value) {
  if (value > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<int64_t>(value);
}

bool ApplyIntegerOperation(int64_t value, int64_t operand,
                           IntegerOperation operation, int64_t* result) {
  if (operation == IntegerOperation::kAdd) {
    if ((operand > 0 &&
         value > std::numeric_limits<int64_t>::max() - operand) ||
        (operand < 0 &&
         value < std::numeric_limits<int64_t>::min() - operand)) {
      return false;
    }
    *result = value + operand;
    return true;
  }

  if ((operand > 0 && value < std::numeric_limits<int64_t>::min() + operand) ||
      (operand < 0 && value > std::numeric_limits<int64_t>::max() + operand)) {
    return false;
  }
  *result = value - operand;
  return true;
}

void UpdateInteger(Client* const client, std::string_view key, int64_t operand,
                   IntegerOperation operation) {
  auto* redis_db = client->Db();
  if (redis_db == nullptr) {
    client->AddReply(reply::FromError("ERR db unavailable"));
    return;
  }

  auto* object = redis_db->MutableLookupKey(key);
  if (object != nullptr &&
      object->Type() != db::RedisObject::ObjectType::kString) {
    client->AddReply(reply::WrongTypeError());
    return;
  }
  int64_t value = 0;
  if (object != nullptr && !utils::ToInt64(object->String(), &value)) {
    client->AddReply(reply::FromError("ERR value is not an integer"));
    return;
  }
  int64_t next = 0;
  if (!ApplyIntegerOperation(value, operand, operation, &next)) {
    client->AddReply(
        reply::FromError("ERR increment or decrement would overflow"));
    return;
  }
  std::string next_value = std::to_string(next);
  if (object != nullptr) {
    *object->MutableString() = std::move(next_value);
  } else if (redis_db->SetKey(
                 key, db::RedisObject::CreateWithString(std::move(next_value)),
                 0) == db::DbStatus::kError) {
    client->AddReply(reply::FromError("ERR failed to set key"));
    return;
  }
  client->MarkModified();
  client->AddReply(reply::FromInt64(next));
}

void HandleIncrement(Client* const client, IntegerOperation operation) {
  const auto& args = client->Args();
  if (args.size() != 1) {
    client->AddReply(reply::WrongNumberOfArguments());
    return;
  }
  UpdateInteger(client, args[0], 1, operation);
}

void HandleIncrementBy(Client* const client, IntegerOperation operation) {
  const auto& args = client->Args();
  if (args.size() != 2) {
    client->AddReply(reply::WrongNumberOfArguments());
    return;
  }
  int64_t operand = 0;
  if (!utils::ToInt64(args[1], &operand)) {
    client->AddReply(reply::FromError("ERR value is not an integer"));
    return;
  }
  UpdateInteger(client, args[0], operand, operation);
}
}  // namespace

void HandleIncr(Client* const client) {
  HandleIncrement(client, IntegerOperation::kAdd);
}

void HandleDecr(Client* const client) {
  HandleIncrement(client, IntegerOperation::kSubtract);
}

void HandleIncrBy(Client* const client) {
  HandleIncrementBy(client, IntegerOperation::kAdd);
}

void HandleDecrBy(Client* const client) {
  HandleIncrementBy(client, IntegerOperation::kSubtract);
}

void HandleAppend(Client* const client) {
  const auto& args = client->Args();
  if (args.size() != 2) {
    client->AddReply(reply::WrongNumberOfArguments());
    return;
  }
  auto* redis_db = client->Db();
  if (redis_db == nullptr) {
    client->AddReply(reply::FromError("ERR db unavailable"));
    return;
  }
  std::string_view key = args[0];
  std::string_view value_to_append = args[1];
  auto* object = redis_db->MutableLookupKey(key);
  if (object != nullptr &&
      object->Type() != db::RedisObject::ObjectType::kString) {
    client->AddReply(reply::WrongTypeError());
    return;
  }
  if (object == nullptr) {
    const auto length = ToReplyInteger(value_to_append.size());
    if (!length.has_value()) {
      client->AddReply(reply::FromError("ERR string length out of range"));
      return;
    }
    if (redis_db->SetKey(
            key,
            db::RedisObject::CreateWithString(std::string(value_to_append)),
            0) == db::DbStatus::kError) {
      client->AddReply(reply::FromError("ERR failed to set key"));
      return;
    }
    client->MarkModified();
    client->AddReply(reply::FromInt64(*length));
    return;
  }
  std::string* const value = object->MutableString();
  value->append(value_to_append);
  client->MarkModified();
  const auto length = ToReplyInteger(value->size());
  client->AddReply(length.has_value()
                       ? reply::FromInt64(*length)
                       : reply::FromError("ERR string length out of range"));
}

void HandleStrLen(Client* const client) {
  const auto& args = client->Args();
  if (args.size() != 1) {
    client->AddReply(reply::WrongNumberOfArguments());
    return;
  }
  auto* redis_db = client->Db();
  if (redis_db == nullptr) {
    client->AddReply(reply::FromError("ERR db unavailable"));
    return;
  }
  const auto result = LookupString(redis_db, args[0]);
  if (result.status == StringStatus::kWrongType) {
    client->AddReply(reply::WrongTypeError());
    return;
  }
  if (result.status == StringStatus::kMissing) {
    client->AddReply(reply::FromInt64(0));
    return;
  }
  const auto length = ToReplyInteger(result.value->size());
  client->AddReply(length.has_value()
                       ? reply::FromInt64(*length)
                       : reply::FromError("ERR string length out of range"));
}

void HandleMGet(Client* const client) {
  const auto& keys = client->Args();
  if (keys.empty()) {
    client->AddReply(reply::WrongNumberOfArguments());
    return;
  }
  auto* redis_db = client->Db();
  if (redis_db == nullptr) {
    client->AddReply(reply::FromError("ERR db unavailable"));
    return;
  }
  std::string encoded = reply::FromArrayHeader(keys.size());
  for (const auto& key : keys) {
    const auto result = LookupString(redis_db, key);
    if (result.status == StringStatus::kOk) {
      reply::AppendBulkString(*result.value, &encoded);
    } else {
      encoded.append(reply::Null(client->Protocol()));
    }
  }
  client->AddReply(std::move(encoded));
}

void HandleMSet(Client* const client) {
  const auto& args = client->Args();
  if (args.empty() || args.size() % 2 != 0) {
    client->AddReply(reply::WrongNumberOfArguments());
    return;
  }
  auto* redis_db = client->Db();
  if (redis_db == nullptr) {
    client->AddReply(reply::FromError("ERR db unavailable"));
    return;
  }
  for (size_t i = 0; i < args.size(); i += 2) {
    redis_db->SetKey(
        args[i], db::RedisObject::CreateWithString(std::string(args[i + 1])),
        0);
  }
  client->MarkModified();
  client->AddReply(reply::FromString("OK"));
}
}  // namespace redis_simple::command::strings
