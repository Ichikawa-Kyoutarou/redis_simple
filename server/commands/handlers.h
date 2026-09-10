#pragma once

namespace redis_simple {
class Client;
}  // namespace redis_simple

namespace redis_simple::command::key {
void HandleDbSize(Client* client);
void HandleDel(Client* client);
void HandleExpire(Client* client);
void HandleExists(Client* client);
void HandleFlushDb(Client* client);
void HandlePersist(Client* client);
void HandlePExpire(Client* client);
void HandlePExpireAt(Client* client);
void HandlePTtl(Client* client);
void HandleRename(Client* client);
void HandleScan(Client* client);
void HandleTtl(Client* client);
void HandleType(Client* client);
void HandleUnlink(Client* client);
}  // namespace redis_simple::command::key

namespace redis_simple::command::session {
void HandleEcho(Client* client);
void HandleHello(Client* client);
void HandlePing(Client* client);
void HandleQuit(Client* client);
}  // namespace redis_simple::command::session

namespace redis_simple::command::persistence {
void HandleBgRewriteAof(Client* client);
void HandleInfo(Client* client);
}  // namespace redis_simple::command::persistence

namespace redis_simple::command::strings {
void HandleAppend(Client* client);
void HandleDecr(Client* client);
void HandleDecrBy(Client* client);
void HandleGet(Client* client);
void HandleIncr(Client* client);
void HandleIncrBy(Client* client);
void HandleMGet(Client* client);
void HandleMSet(Client* client);
void HandleSet(Client* client);
void HandleStrLen(Client* client);
}  // namespace redis_simple::command::strings

namespace redis_simple::command::lists {
void HandleLPush(Client* client);
void HandleRPush(Client* client);
void HandleLPop(Client* client);
void HandleRPop(Client* client);
void HandleLLen(Client* client);
void HandleLRange(Client* client);
void HandleLIndex(Client* client);
void HandleLSet(Client* client);
void HandleLRem(Client* client);
void HandleLTrim(Client* client);
}  // namespace redis_simple::command::lists

namespace redis_simple::command::sets {
void HandleSAdd(Client* client);
void HandleSCard(Client* client);
void HandleSDiff(Client* client);
void HandleSIsMember(Client* client);
void HandleSInter(Client* client);
void HandleSMembers(Client* client);
void HandleSRem(Client* client);
void HandleSUnion(Client* client);
}  // namespace redis_simple::command::sets

namespace redis_simple::command::zsets {
void HandleZAdd(Client* client);
void HandleZCard(Client* client);
void HandleZCount(Client* client);
void HandleZRange(Client* client);
void HandleZRangeByScore(Client* client);
void HandleZRank(Client* client);
void HandleZRevRange(Client* client);
void HandleZRem(Client* client);
void HandleZScore(Client* client);
}  // namespace redis_simple::command::zsets

namespace redis_simple::command::hashes {
void HandleHDel(Client* client);
void HandleHExists(Client* client);
void HandleHGet(Client* client);
void HandleHGetAll(Client* client);
void HandleHIncrBy(Client* client);
void HandleHKeys(Client* client);
void HandleHLen(Client* client);
void HandleHMGet(Client* client);
void HandleHSet(Client* client);
void HandleHVals(Client* client);
}  // namespace redis_simple::command::hashes
