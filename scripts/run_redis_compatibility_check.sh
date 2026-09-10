#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <redis_simple_binary>" >&2
  exit 1
fi

server_binary="$1"
redis_cli="${REDIS_CLI_BIN:-redis-cli}"
reference_host="${REDIS_REFERENCE_HOST:-127.0.0.1}"
reference_port="${REDIS_REFERENCE_PORT:-6379}"
server_port="${REDIS_SIMPLE_PORT:-8080}"
server_log="$(mktemp "${TMPDIR:-/tmp}/redis_simple_compat.XXXXXX")"

cleanup() {
  if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
    kill -TERM "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
  fi
  rm -f "${server_log}"
}
trap cleanup EXIT INT TERM

"${server_binary}" >"${server_log}" 2>&1 &
server_pid=$!

for _ in {1..50}; do
  if "${redis_cli}" -h 127.0.0.1 -p "${server_port}" --raw \
      TYPE compatibility_ready >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! kill -0 "${server_pid}" 2>/dev/null; then
  cat "${server_log}" >&2
  exit 1
fi

reference() {
  "${redis_cli}" -h "${reference_host}" -p "${reference_port}" --raw "$@"
}

actual() {
  "${redis_cli}" -h 127.0.0.1 -p "${server_port}" --raw "$@"
}

compare() {
  local expected received
  expected="$(reference "$@")"
  received="$(actual "$@")"
  if [[ "${received}" != "${expected}" ]]; then
    echo "Redis compatibility mismatch: $*" >&2
    echo "expected: ${expected}" >&2
    echo "actual:   ${received}" >&2
    return 1
  fi
}

reference FLUSHDB >/dev/null
actual FLUSHDB >/dev/null

compare SET "key with spaces" "value with spaces"
compare GET "key with spaces"
compare STRLEN "key with spaces"
compare TYPE "key with spaces"
compare SET conditional_key initial NX
compare SET conditional_key ignored NX
compare SET conditional_key updated XX GET
compare GET conditional_key
compare SET conditional_missing ignored XX
future_seconds="$(($(date +%s) + 60))"
future_milliseconds="$((future_seconds * 1000))"
compare SET exat_key value EXAT "${future_seconds}"
compare GET exat_key
compare SET pxat_key value PXAT "${future_milliseconds}"
compare GET pxat_key
compare SET counter_key 10
compare INCRBY counter_key 5
compare DECRBY counter_key 3
compare SET ttl_key value PX 999
compare TTL ttl_key
reference_pttl="$(reference PTTL ttl_key)"
actual_pttl="$(actual PTTL ttl_key)"
if ((reference_pttl <= 0 || reference_pttl > 999 || actual_pttl <= 0 ||
     actual_pttl > 999)); then
  echo "PTTL compatibility check returned an invalid range" >&2
  echo "reference: ${reference_pttl}, actual: ${actual_pttl}" >&2
  exit 1
fi
compare PERSIST ttl_key
compare ZADD ordered 1 one 2 two 3 three
compare ZRANGE ordered -10 -1
compare ZRANGE ordered 0 -1 REV
compare ZRANGE ordered 3 1 BYSCORE REV
compare ZRANGE ordered 1 3 BYSCORE LIMIT 1 1
compare ZRANGE ordered 0 -1 WITHSCORES
compare GET missing_key

"${redis_cli}" -3 -h 127.0.0.1 -p "${server_port}" --raw \
  GET missing_resp3_key >/dev/null

echo "Redis compatibility checks passed"
