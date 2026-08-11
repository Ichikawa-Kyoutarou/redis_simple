# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project Style

- Keep this a modern C++17 project.
- Follow Google C++ style and the checked-in `.clang-format`.
- Prefer clear, simple C++17 code over legacy C++11 patterns.
- Keep naming consistent with nearby code.
- Use `Create()` for owning factory functions and return `std::unique_ptr`
  instead of owning raw pointers.
- Prefer concise accessor names such as `Type()`, `Encoding()`, and
  `TotalBytes()` over Java-style `Get...` names for new or renamed APIs.
- Prefer `std::string_view` for read-only string inputs and visitor-style
  traversal for hot paths; keep vector-returning helpers for convenience APIs
  and tests, not command execution paths that can stream replies directly.
- Command handlers should read arguments through `CommandArgs`
  (`std::string_view`s into the client query buffer). Copy arguments only at an
  ownership boundary, such as storing a key/value in the DB or data structure.
- Keep request decoding incremental and zero-copy. Standard RESP arrays are the
  primary wire format; inline commands remain a compatibility path.
- Keep protocol-sensitive replies based on the client's negotiated RESP
  version. Connections default to RESP2 and may switch through `HELLO`.
- Pass `CommandArgs` by const reference instead of copying its view vector.
  Move completed `std::string` replies into `Client::AddReply`; use the
  `std::string_view` overload only for borrowed reply data.
- Add comments only when they clarify non-obvious behavior.
- Keep command handler declarations grouped in `server/commands/handlers.h`;
  avoid per-command headers unless a handler becomes a broader shared API.
- Keep command names, arity, access mode, and key positions in the sorted
  metadata table in `server/commands/command.cpp`. Preserve its allocation-free,
  case-insensitive lookup path.
- When adding or changing supported commands, update the README command
  coverage list and the relevant command-family integration test.
- Keep reply encoding helpers directly under `server/`, and keep database
  state and Redis object wrappers under `server/db/`.
- Keep production and command integration servers on the shared `Server::Run`
  lifecycle. Signal handlers may only update signal-safe state; cleanup belongs
  on the event-loop thread.
- Keep AOF propagation at the successful command-mutation boundary. Preserve
  command order, encode relative TTLs as absolute `PEXPIREAT` records, and do
  not copy argument payloads beyond the owned persistence record.
- Keep AOF rewrite snapshots visitor-based and preserve the original AOF until
  the snapshot and buffered mutation delta have been synced and atomically
  installed. The normal append path should pay no copy cost when no rewrite is
  active.
- Bound AOF snapshot memory and replay command sizes together. Split large
  strings into replay-safe commands and batch collections by encoded bytes, not
  only element count.
- Complete durable AOF replacement in this order: sync the temporary file,
  rename it, then sync the parent directory. Keep failed rewrites observable
  through `INFO persistence` without disabling a still-healthy original AOF.

## Build And Test

Use CMake presets:

```sh
cmake --preset debug
cmake --build --preset debug
```

Run unit and integration tests separately:

```sh
ctest --preset debug -L unit --output-on-failure
ctest --preset debug -L integration --output-on-failure
```

When changing behavior shared with Redis and a reference server is available,
run:

```sh
scripts/run_redis_compatibility_check.sh build/debug/redis_simple
```

Run release and sanitizer checks after changes to ownership, memory layout,
assertions, or low-level data structures:

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cmake --preset sanitizer
cmake --build --preset sanitizer
ctest --preset sanitizer --output-on-failure
scripts/run_leak_check.sh
```

Build and run the bounded Clang libFuzzer smoke tests after changes to request
parsing, Redis data types, core containers, buffers, expiration, persistence,
or event-loop behavior:

```sh
cmake --preset fuzz
cmake --build --preset fuzz --target redis_simple_fuzzers
ctest --preset fuzz -L fuzz --output-on-failure
```

On macOS, configure the fuzz preset with Homebrew LLVM on `PATH`; Apple Clang
does not include the libFuzzer runtime.

The leak-check script uses Apple `leaks` for the macOS unit-test binary and
explicit LeakSanitizer options for the complete Linux sanitizer test suite.

Use Docker for a local Linux build and test check from macOS:

```sh
scripts/run_linux_docker_check.sh
```

The helper prepares its cached Ubuntu toolchain without mounting the
repository. Its build and test container has networking disabled and mounts the
repository read-only.

Before committing, run the relevant build and tests.

## Test Layout

- Unit tests stay colocated with implementation files as `*_test.cpp`.
- Register unit tests in CTest by GoogleTest suite through
  `redis_simple_add_gtest_suite`, so failures identify the affected suite while
  preserving same-suite fixture behavior.
- Integration tests live under `integration/`.
- Stateful libFuzzer harnesses live under `fuzz/`, compare operations against
  simple reference models where practical, and use bounded CTest smoke runs
  for CI.
- Current integration coverage should stay focused:
  - `integration/commands/`
  - `integration/aof_client_test.cpp`
  - `integration/tcp/`
- Keep server option and graceful shutdown coverage in the lifecycle integration
  test rather than introducing a separate command-test server implementation.
- Keep AOF restart, manual and automatic rewrite compaction, shutdown flush,
  expiration, and cross-data-type recovery in the dedicated AOF integration
  test. Use injected file operations for deterministic write, sync, rename, and
  directory-sync failure unit tests.
- Keep command-family integration tests split by area, including key, string,
  set, list, zset, hash, and connection commands.
- Register integration command tests as separate CTest entries by command
  family, so failures identify the affected area without log digging.
- Keep exact ordering assertions for ordered command results such as sorted-set
  ranges; do not sort actual output in compatibility tests.
- Project runner scripts live under `scripts/`.
- Do not add manual log-inspection tests. Tests should assert behavior and
  return nonzero on failure.

## Project Management

- Run clang-format 18 on changed C/C++ files before committing or pushing. The
  format script rejects other major versions to keep local and CI output equal.
- Use `scripts/format.sh --check` and `scripts/run_clang_tidy.sh` for local
  quality checks; clang-tidy warnings are treated as errors.
- Never put required side effects inside `assert`; release builds must preserve
  behavior when assertions are disabled.
- Always update relevant docs, including `README.md` and this `AGENTS.md`, when
  changing build, test, workflow, or project conventions.
- Keep CMake target-based. Source files are discovered by scoped directory
  globs in `CMakeLists.txt`; exclude generated, test, or entry-point sources
  explicitly when they do not belong in a library target.
- Keep event-loop pollers platform-selected in `CMakeLists.txt`: `kqueue` for
  macOS and `epoll` for Linux.
- Keep `CMakeLists.txt`, `CMakePresets.json`, `.github/workflows/ci.yml`,
  `.clang-format`, `.clang-tidy`, and `.editorconfig` aligned with project
  conventions.
- Keep CI branch pushes limited to `main`; validate feature branches through
  pull requests targeting `main`, and cancel superseded runs for the same ref.
- Avoid unrelated refactors while making focused changes.
- Do not reintroduce stale mock targets that are not part of CTest or normal
  project workflows.
