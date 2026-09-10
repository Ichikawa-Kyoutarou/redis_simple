#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "logging/logger.h"
#include "server/client.h"
#include "server/commands/handlers.h"
#include "server/commands/string/args.h"
#include "server/db/db.h"
#include "server/reply.h"
#include "utils/string_utils.h"
#include "utils/time_utils.h"

namespace redis_simple::command::strings {
namespace {
int ParseArgs(const CommandArgs& args, StringArgs* string_args);
int ParseSetOption(const CommandArgs& args, size_t* idx,
                   StringArgs* string_args);
bool ExpireAtFromTtl(int64_t ttl, int64_t multiplier, int64_t now,
                     int64_t* expire);
bool MillisecondsFromSeconds(int64_t seconds, int64_t* milliseconds);
enum class SetStatus : uint8_t {
  kSet,
  kNotSet,
  kWrongType,
  kError,
};

struct SetResult {
  SetStatus status{SetStatus::kError};
  std::optional<std::string> old_value;
};

SetResult Set(db::RedisDb* redis_db, const StringArgs& args);
}  // namespace

void HandleSet(Client* const client) {
  RS_LOG_DEBUG("set command called\n");
  StringArgs args;
  if (ParseArgs(client->Args(), &args) < 0) {
    client->AddReply(reply::SyntaxError());
    return;
  }

  if (auto* redis_db = client->Db()) {
    SetResult result = Set(redis_db, args);
    if (result.status == SetStatus::kWrongType) {
      client->AddReply(reply::WrongTypeError());
      return;
    }
    if (result.status == SetStatus::kError) {
      client->AddReply(reply::FromError("ERR failed to set key"));
      return;
    }
    if (result.status == SetStatus::kNotSet) {
      client->AddReply(result.old_value.has_value()
                           ? reply::FromBulkString(*result.old_value)
                           : reply::Null(client->Protocol()));
      return;
    }
    client->MarkModified();
    if (!args.return_old_value) {
      client->AddReply(reply::FromString("OK"));
    } else if (result.old_value.has_value()) {
      client->AddReply(reply::FromBulkString(*result.old_value));
    } else {
      client->AddReply(reply::Null(client->Protocol()));
    }
  } else {
    RS_LOG_DEBUG("db unavailable\n");
    client->AddReply(reply::FromError("ERR db unavailable"));
  }
}

namespace {

int ParseArgs(const CommandArgs& args, StringArgs* string_args) {
  if (args.size() < 2) {
    RS_LOG_DEBUG("invalid args\n");
    return -1;
  }
  string_args->key = args[0];
  string_args->value = args[1];
  string_args->expire = 0;
  string_args->flags = 0;
  string_args->condition = SetCondition::kNone;
  string_args->return_old_value = false;
  for (size_t i = 2; i < args.size();) {
    if (ParseSetOption(args, &i, string_args) < 0) {
      return -1;
    }
  }
  return 0;
}

int ParseSetOption(const CommandArgs& args, size_t* const idx,
                   StringArgs* const string_args) {
  const std::string_view option = args[*idx];
  const bool if_missing = utils::EqualsIgnoreCase(option, "NX");
  const bool if_exists = utils::EqualsIgnoreCase(option, "XX");
  if (if_missing || if_exists) {
    if (string_args->condition != SetCondition::kNone) {
      return -1;
    }
    string_args->condition =
        if_missing ? SetCondition::kIfMissing : SetCondition::kIfExists;
    ++(*idx);
    return 0;
  }
  if (utils::EqualsIgnoreCase(option, "GET")) {
    if (string_args->return_old_value) {
      return -1;
    }
    string_args->return_old_value = true;
    ++(*idx);
    return 0;
  }
  if (utils::EqualsIgnoreCase(option, "KEEPTTL")) {
    if (string_args->expire > 0 ||
        db::HasFlag(string_args->flags, db::SetKeyFlag::kKeepTtl)) {
      return -1;
    }
    string_args->flags |= db::ToInt(db::SetKeyFlag::kKeepTtl);
    ++(*idx);
    return 0;
  }
  const bool relative_seconds = utils::EqualsIgnoreCase(option, "EX");
  const bool relative_milliseconds = utils::EqualsIgnoreCase(option, "PX");
  const bool absolute_seconds = utils::EqualsIgnoreCase(option, "EXAT");
  const bool absolute_milliseconds = utils::EqualsIgnoreCase(option, "PXAT");
  if (!relative_seconds && !relative_milliseconds && !absolute_seconds &&
      !absolute_milliseconds) {
    return -1;
  }
  if ((string_args->flags & db::ToInt(db::SetKeyFlag::kKeepTtl)) != 0 ||
      string_args->expire > 0 || *idx + 1 >= args.size()) {
    return -1;
  }
  int64_t expiration = 0;
  if (!utils::ToInt64(args[*idx + 1], &expiration) || expiration <= 0) {
    return -1;
  }
  constexpr int64_t kMillisecondsPerSecond = 1000;
  if (relative_seconds || relative_milliseconds) {
    const int64_t multiplier = relative_seconds ? kMillisecondsPerSecond : 1;
    if (!ExpireAtFromTtl(expiration, multiplier, utils::NowInMilliseconds(),
                         &string_args->expire)) {
      return -1;
    }
  } else if (absolute_seconds) {
    if (!MillisecondsFromSeconds(expiration, &string_args->expire)) {
      return -1;
    }
  } else {
    string_args->expire = expiration;
  }
  if (string_args->expire <= 0) {
    return -1;
  }
  *idx += 2;
  return 0;
}

bool ExpireAtFromTtl(int64_t ttl, int64_t multiplier, int64_t now,
                     int64_t* expire) {
  if (ttl <= 0) {
    return false;
  }
  if (ttl > std::numeric_limits<int64_t>::max() / multiplier) {
    return false;
  }
  const int64_t ttl_ms = ttl * multiplier;
  if (ttl_ms > std::numeric_limits<int64_t>::max() - now) {
    return false;
  }
  *expire = now + ttl_ms;
  return true;
}

bool MillisecondsFromSeconds(int64_t seconds, int64_t* milliseconds) {
  constexpr int64_t kMillisecondsPerSecond = 1000;
  if (seconds <= 0 ||
      seconds > std::numeric_limits<int64_t>::max() / kMillisecondsPerSecond) {
    return false;
  }
  *milliseconds = seconds * kMillisecondsPerSecond;
  return true;
}

SetResult Set(db::RedisDb* redis_db, const StringArgs& args) {
  const db::RedisObject* const existing = redis_db->LookupKey(args.key);
  std::optional<std::string> old_value;
  if (args.return_old_value && existing != nullptr) {
    if (existing->Type() != db::RedisObject::ObjectType::kString) {
      return {SetStatus::kWrongType, std::nullopt};
    }
    old_value = existing->String();
  }

  const bool exists = existing != nullptr;
  if ((args.condition == SetCondition::kIfMissing && exists) ||
      (args.condition == SetCondition::kIfExists && !exists)) {
    return {SetStatus::kNotSet, std::move(old_value)};
  }

  auto value = db::RedisObject::CreateWithString(std::string(args.value));
  const auto status =
      redis_db->SetKey(args.key, std::move(value), args.expire, args.flags);
  return {status == db::DbStatus::kError ? SetStatus::kError : SetStatus::kSet,
          std::move(old_value)};
}
}  // namespace
}  // namespace redis_simple::command::strings
