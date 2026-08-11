# redis_simple

`redis_simple` is a small Redis-inspired server implemented in modern C++17. It
includes a TCP server, event loop, command handlers, in-memory data structures,
optional append-only persistence, a simple CLI client, unit tests, integration
tests, and memory benchmarks.

The project is intentionally compact: implementation-level unit tests live next
to the code they cover, while runnable end-to-end checks live under
`integration/`.

## Requirements

- CMake 3.21+
- A C++17 compiler
- Git submodules for third-party dependencies
- macOS (`kqueue`) or Linux (`epoll`) for the event-loop poller

## Setup

```sh
git submodule update --init --recursive
cmake --preset debug
cmake --build --preset debug
```

The debug preset builds into `build/debug`. A release preset is also available:

```sh
cmake --preset release
cmake --build --preset release
```

## Run

Start the server:

```sh
./build/debug/redis_simple
```

The server binds to `127.0.0.1:8080` by default. Configure either value at
startup and stop cleanly with `SIGINT` or `SIGTERM`:

```sh
./build/debug/redis_simple --bind 0.0.0.0 --port 6380
./build/debug/redis_simple --help
```

Enable AOF persistence with Redis-style fsync policies:

```sh
./build/debug/redis_simple --appendonly yes \
  --appendfilename appendonly.aof --appendfsync everysec \
  --auto-aof-rewrite-min-size 67108864 \
  --auto-aof-rewrite-percentage 100
```

AOF is disabled by default. When enabled, `appendonly.aof` is the default path
and `everysec` is the default fsync policy. `always` writes and syncs each
mutation before continuing, while `no` delegates periodic durability to the
operating system. Background policies move encoded commands into a bounded
writer queue and use scatter/gather writes without copying command payloads.
Graceful shutdown drains and syncs the queue. `BGREWRITEAOF` compacts command
history into a point-in-time database snapshot. Snapshot records are produced in
bounded blocks while a background thread writes them; large strings are split
into replay-safe commands, and collection batches are limited by encoded bytes.
Mutations that overlap the rewrite share one immutable encoded record between
the normal writer and rewrite delta queues. The replacement file is synced,
renamed atomically, and followed by a parent-directory sync before it becomes
durable.

Automatic rewriting starts after the AOF reaches 64 MiB and grows by 100% from
the last rewrite or startup size. The two `--auto-aof-rewrite-*` options override
those defaults; a percentage of `0` disables automatic rewriting. Use
`INFO persistence` to inspect rewrite progress, the last rewrite/error status,
current and base sizes, and queued bytes. An unrecoverable append or sync error
marks persistence unhealthy and stops the server instead of continuing without
durable command history.

Clients can send standard RESP arrays of bulk strings or the legacy inline
syntax. RESP2 is the default reply protocol; `HELLO 3` switches a connection to
RESP3, including native null, map, set, and double replies. Request buffers are
capped at 64 MiB. Pending output pauses reads at 8 MiB, resumes below 4 MiB,
and closes the connection before exceeding 64 MiB.

In another terminal, run a mock command client or your own TCP client against
`localhost:8080`.

## Command Coverage

The server supports Redis-style RESP replies for the implemented command set,
including protocol errors such as `ERR wrong number of arguments` and
`WRONGTYPE`.

- Keys: `DEL`, `UNLINK`, `EXISTS`, `TYPE`, `EXPIRE`, `PEXPIRE`, `PEXPIREAT`,
  `TTL`, `PTTL`, `PERSIST`, `RENAME`, `DBSIZE`, `FLUSHDB`, `SCAN` with `MATCH`
  and `COUNT`
- Strings: `GET`, `SET` with `EX`, `PX`, and `KEEPTTL`, `INCR`, `DECR`,
  `APPEND`, `MGET`, `MSET`
- Lists: `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LRANGE`, `LINDEX`, `LSET`,
  `LREM`, `LTRIM`
- Sets: `SADD`, `SCARD`, `SREM`, `SMEMBERS`, `SISMEMBER`, `SINTER`, `SUNION`,
  `SDIFF`
- Sorted sets: `ZADD`, `ZCARD`, `ZREM`, `ZRANK`, `ZRANGE`, `ZREVRANGE`,
  `ZRANGEBYSCORE`, `ZCOUNT`, `ZSCORE`
- Hashes: `HSET`, `HGET`, `HDEL`, `HLEN`, `HEXISTS`, `HGETALL`, `HMGET`,
  `HKEYS`, `HVALS`, `HINCRBY`
- Persistence: `BGREWRITEAOF`, `INFO [persistence]`
- Connection: `HELLO` with RESP2 and RESP3 negotiation, `PING`, `ECHO`, `QUIT`

`UNLINK` detaches keys synchronously and releases their values on a background
worker. Command names, arity, access mode, and key positions are held in one
allocation-free registry used for case-insensitive dispatch and early argument
validation.

When enabled, AOF records successful mutations as canonical RESP commands and
replays them before the server accepts clients. Relative expirations are stored
as absolute millisecond timestamps, incomplete final records are truncated,
and corruption before the final record prevents startup. AOF rewrites traverse
collection values through borrowed views, retain concurrent mutations in a
bounded delta queue, preserve the original file if rewriting fails before the
atomic replacement, and retry failed automatic rewrites after a short delay.

## Test

Run the full test suite through CTest:

```sh
ctest --preset debug
```

Run unit and integration tests separately:

```sh
ctest --preset debug -L unit --output-on-failure
ctest --preset debug -L integration --output-on-failure
```

Release and sanitizer presets are available for checking behavior with
assertions disabled and for detecting memory or undefined-behavior defects:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure

cmake --preset sanitizer
cmake --build --preset sanitizer
ctest --preset sanitizer --output-on-failure
```

Run the platform-specific memory leak check after building the corresponding
preset:

```sh
scripts/run_leak_check.sh
```

On macOS, the script runs the complete debug unit-test binary under Apple's
`leaks` tool because Apple AddressSanitizer does not provide LeakSanitizer. On
Linux, it runs the complete sanitizer CTest suite with LeakSanitizer explicitly
enabled.

Build the opt-in Clang libFuzzer targets and run their bounded CTest smoke
checks with:

```sh
cmake --preset fuzz
cmake --build --preset fuzz --target redis_simple_fuzzers
ctest --preset fuzz -L fuzz --output-on-failure
```

The fuzz targets exercise incremental RESP parsing; listpack, quicklist, Dict,
Skiplist, and IntSet mutation; Redis list, set, hash, and sorted-set behavior;
dynamic and reply buffers; database expiration; and deterministic event-loop
callbacks. AOF replay also has a bounded malformed-input target. Every target
runs under AddressSanitizer and
UndefinedBehaviorSanitizer. On macOS, put Homebrew LLVM on `PATH` before
configuring because Apple Clang does not ship a libFuzzer runtime:

```sh
brew install llvm
PATH="$(brew --prefix llvm)/bin:${PATH}" cmake --preset fuzz
```

This runs:

- `redis_simple_unit_<SuiteName>`: unit tests compiled into
  `redis_simple_tests` and registered in CTest by GoogleTest suite.
- `redis_simple_integration_tcp`: TCP client/server integration check.
- `redis_simple_integration_server_lifecycle`: command-line option, nondefault
  bind, and graceful shutdown checks.
- `redis_simple_integration_aof`: production-server append, background rewrite,
  compaction, shutdown, restart, expiration, and cross-data-type recovery checks.
- `redis_simple_integration_command_connection`: connection command integration
  checks.
- `redis_simple_integration_command_key`: generic key command integration
  checks.
- `redis_simple_integration_command_string`: string command integration checks.
- `redis_simple_integration_command_set`: set command integration checks.
- `redis_simple_integration_command_list`: list command integration checks.
- `redis_simple_integration_command_zset`: sorted-set command integration
  checks.
- `redis_simple_integration_command_hash`: hash command integration checks.

For debugging a single executable directly:

```sh
./build/debug/redis_simple_tests
./build/debug/mock_set_client
```

For debugging one unit-test suite directly:

```sh
./build/debug/redis_simple_tests --gtest_filter=ListPackTest.*
```

The integration clients return nonzero on failed expectations, so they are safe
to run in CI instead of relying on manual log inspection.

When a reference Redis server is available on port 6379, run the focused
differential protocol and command check with:

```sh
scripts/run_redis_compatibility_check.sh build/debug/redis_simple
```

The script uses `redis-cli` against both servers and covers RESP requests,
spaces in arguments, expiration replies, and sorted-set range semantics.

## Benchmarks

```sh
./build/debug/memory_benchmark
```

Google Benchmark's own tests are disabled in this project; the benchmark target
above is the project benchmark executable.

## Project Layout

```text
cli/                  Simple client and RESP parsing
connection/           Connection abstraction
event_loop/           Event loop with kqueue and epoll pollers
fuzz/                 Stateful libFuzzer harnesses and reference models
integration/commands/ Server/client command integration tests
integration/aof_client_test.cpp  AOF restart integration client
integration/tcp/      TCP client/server integration tests
logging/              Project logging wrapper
memory/               Core in-memory data structures
scripts/              Project automation and CTest runner scripts
server/               Server, client connection glue, commands, replies, DB
data_types/           Redis hash, list, set, and sorted-set implementations
tcp/                  TCP helpers
utils/                Small shared utilities
benchmarks/           Memory/data-structure benchmarks
```

Command handler declarations are grouped in `server/commands/handlers.h`; the
individual command `.cpp` files keep implementation details local.
Reply encoding helpers live directly under `server/`, while database state and
Redis object wrappers stay under `server/db/`. The AOF writer and replay logic
also live directly under `server/` because they coordinate command execution,
database loading, and server lifecycle.

## Tooling

Project management files are checked in:

- `CMakePresets.json` for reproducible configure/build/test commands.
- `.clang-format` for Google-style formatting with clang-format 18.
- `.clang-tidy` for static-analysis defaults.
- `.editorconfig` for editor consistency.
- `.github/workflows/ci.yml` for CI on push and pull request.
- `AGENTS.md` for AI coding-agent project guidance.

Format project C++ files with:

```sh
cmake --build --preset debug --target format
```

Formatting is pinned to clang-format 18 so local and CI output remains stable.
Install it with `brew install llvm@18` on macOS or `apt install clang-format-18`
on Ubuntu. `scripts/format.sh` discovers those installations and honors
`CLANG_FORMAT_BIN` when it points to a clang-format 18 executable.

Check formatting without modifying files with:

```sh
scripts/format.sh --check
```

Run clang-tidy with the project wrapper:

```sh
scripts/run_clang_tidy.sh
```

The wrapper uses `build/debug/compile_commands.json` by default and discovers
Homebrew's LLVM toolchain on macOS. Set `BUILD_DIR`, `CLANG_TIDY_BIN`, or
`RUN_CLANG_TIDY_BIN` to override those defaults.

The root `CMakeLists.txt` is target-based. Production, test, and benchmark
sources are discovered from scoped project directories with
`GLOB_RECURSE CONFIGURE_DEPENDS`, while test files and executable entry points
are excluded from library targets explicitly. Platform-specific event-loop
pollers are selected at configure time: `kqueue` on macOS and `epoll` on Linux.

Run the Linux build and test flow locally from macOS through Docker:

```sh
scripts/run_linux_docker_check.sh
```

On its first run, the Docker helper creates a local Ubuntu 24.04 toolchain image
without mounting the repository. Build and test runs then disable container
networking, mount the repository read-only, drop Linux capabilities, and build
under a temporary filesystem. This validates the Linux `epoll` poller without
writing Linux artifacts into the working tree or exposing source code to the
container network. Set `DOCKER_BASE_IMAGE`, `DOCKER_TOOLCHAIN_IMAGE`, or
`LINUX_DOCKER_BUILD_DIR` to override the defaults.

## C++ Style

The code follows Google C++ style with project-local conventions: owning factory
functions are named `Create()` and return `std::unique_ptr`, while concise
accessors such as `Type()`, `Encoding()`, and `TotalBytes()` are preferred over
Java-style `Get...` names.

Hot paths prefer `std::string_view` for read-only inputs and visitor-style
traversal when callers can encode or rebuild results directly. Vector-returning
helpers remain useful for convenience APIs and tests, but command handlers
should avoid materializing extra copies when they can stream replies in one
pass.

Command handlers borrow their argument views directly from the client query
buffer. Completed string replies are moved into the output queue, and the queue
reuses scatter/gather metadata across partial writes.

## CI

CI runs for pull requests targeting `main` and for pushes to `main`. A new run
for the same pull request or branch cancels any older run still in progress,
avoiding duplicate branch-push runs and work on superseded commits.

GitHub Actions runs debug, release, and ASan/UBSan matrices on macOS and Ubuntu,
so both event-loop pollers are built and tested. macOS also runs Apple `leaks`;
Ubuntu runs LeakSanitizer, clang-format 18, clang-tidy, and bounded libFuzzer
smoke tests. A separate Ubuntu job compares supported behavior with a real
Redis instance through `redis-cli`:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -L unit --output-on-failure
ctest --preset debug -L integration --output-on-failure
```
