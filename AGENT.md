# Repository guidance

## Project overview

Lightning++ is a C++20 HTTP web framework in the `lightning` namespace. The
current implementation builds the `lightning` static library. Check source and
tests before assuming TODO features are implemented.

## Repository map

- `src/include/lightning/`: public headers and API types.
- `src/lib/`: `.cxx` implementations and the private `http_request_parser.h`.
- `src/test/`: GoogleTest tests for headers, parsing, responses, and socket behavior.
- `benchmarks/`: optional Google Benchmark cases, HTTP fixture, workload specs,
  Python load runner, and advisory result comparison.
- `CMakeLists.txt`, `src/*/CMakeLists.txt`: dependencies, library, and test targets.
- `cmake/`: compiler flags, Conan integration, versioning, and Doxygen setup.
- `conanfile.txt`: pinned Asio, llhttp, GoogleTest, fmt, and nlohmann/json dependencies.
- `build.sh`: build, test, sanitizer, Docker, and documentation entry point.
- `docker/`, `.github/workflows/main.yml`: compiler environments and CI checks.
- `doc/`: Doxygen configuration and bundled theme assets.

Asio provides asynchronous networking, llhttp parses HTTP messages, and
CxxLogger is fetched at a pinned Git revision by CMake.

## Build and validation

Run commands from the repository root. Prerequisites are CMake 3.20+, Conan
1.63+ or 2.x, and GCC 13+, Clang 17+, or AppleClang 15+. Make is the default
generator; pass `ninja` to use Ninja. First configuration may download
dependencies and create a missing default Conan profile; existing profiles
are preserved.

```bash
./build.sh                            # Debug build
./build.sh test                       # Debug build and full test suite
./build.sh release test               # Release build; warnings are errors
./build.sh test asan=on ubsan=on       # Debug memory and undefined-behavior checks
./build.sh test tsan=on               # Separate thread-sanitizer run
./build.sh docker=gcc13 debug test    # Linux GCC environment
./build.sh docker=clang17 debug test  # Linux Clang environment
./build.sh release doc                # Generate documentation; requires Doxygen
DEFAULT_BUILD_DIR=build/coverage ./build.sh coverage=on test # LLVM coverage and floors
./build.sh bench test                 # Isolated Release benchmarks and correctness smoke tests
```

Build output defaults to `build/debug/` or `build/release/`. The script defaults
to Debug, while direct CMake configuration defaults to Release. Sanitizer flags
reset to off on each script invocation. ASan and UBSan instrument Debug builds;
TSan instruments both Debug and Release. Never combine ASan and TSan.

After a configured build, use these commands for targeted iteration:

```bash
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
build/debug/bin/test_lightning --gtest_filter='HttpRequest.*'
```

CMake registers `test_lightning` with a 60-second timeout. Ordinary builds also
register `installed_consumer` (120 seconds): it checks library-only configuration,
installs and relocates the package, and builds/runs an independent consumer.
Coverage builds add `coverage_tool`, and the `coverage` build target measures
only project code with default floors of 90% lines and 85% branches. Coverage
requires single-config Debug Clang/AppleClang, matching LLVM tools, and Python
3.9+; keep it separate from sanitizers. Do not count test/dependency code or reuse
old profiles to improve metrics. `BUILD_TESTING=OFF` skips test targets/discovery;
`LIGHTNING_USE_CONAN=OFF` supports already-provisioned dependencies. Preserve the
relocatable `lightning::lightning` export and its public C++20 requirements.
Use GoogleTest filters to select individual suites. Source files are discovered
with CMake globs, so rerun configuration (or `./build.sh test`) after adding a
`.cxx` file. Use a separate build directory via `DEFAULT_BUILD_DIR` when changing
generators or compilers, or deliberately clean the selected configuration;
`./build.sh clean` removes that configuration's build directory.

For behavior changes, add or update relevant regression tests and run
`./build.sh test`. Use sanitizer checks for ownership, parsing, or concurrency
changes as appropriate. Check Release when changing code that may trigger
warnings. Documentation-only edits do not require compiling the project.
Report commands run and any checks that could not complete.

Benchmark timing is opt-in: `bench` builds targets in `build/benchmarks/release`,
then executables/scripts run measurements explicitly. Google Benchmark 1.9.1 is
optional and must not enter the library export/dependency path. CI runs
`benchmark_micro_smoke` and `benchmark_harness` for correctness only. Never use
smoke or instrumented timings as baselines. Keep raw repetitions and provenance,
validate responses, and report HTTP loopback/closed-loop limitations. Do not
silently update baselines or enforce absolute timing gates on shared runners.

## Code conventions

Follow the surrounding style: two-space indentation, `.h` headers, `.cxx`
implementations, `PascalCase` types, `camelCase` methods, `_camelCase` private
members, and `k`-prefixed enum values. Existing code generally places a space
before nonempty call parentheses and uses braced initialization. Preserve
license notices and avoid unrelated formatting changes.

Keep public declarations in `src/include/lightning/` and implementation details
in `src/lib/`. Maintain C++20 compatibility and avoid adding dependencies or
changing dependency pins unless the task requires it.

## HTTP and concurrency contracts

- Requests own parsed headers and body bytes. Bodies use `std::vector<uint8_t>`
  and must preserve embedded nulls; do not treat network data as C strings.
- `HttpRequest::parse()` accepts exactly one complete request and rejects
  incomplete, invalid, or multiple messages. The private incremental parser
  and connection code handle fragmentation and pipelining.
- Route paths and response header values are copied, including temporary inputs.
  Preserve buffer lifetimes across asynchronous reads and writes.
- Responses default to 200. Invalid requests receive 400. Callback errors enter
  the ordered `onError()` chain; unhandled errors receive 500 and close the connection.
- Middleware and routes share an ordered dispatcher. `Next` is synchronous,
  single-use, and confined to its callback thread. Registrations are snapshotted
  per request; request-local context is stored in `HttpRequest::locals`.
- Routes support whole-segment `:name` parameters and final `*name` wildcards.
  Captures are decoded once into scoped `params`; malformed captures default to
  400 through `RouteDecodeError`. Literal matching and mount boundaries use raw paths.
- Router mounts share live configuration and snapshot the reachable graph before
  callbacks. Preserve parent `path`, `baseUrl`, and `params` across nested routing,
  fallthrough, and error propagation; reject mount cycles.
- REST parsing is opt-in: `queryParams()` returns owned repeated values;
  `json()`/`urlencoded()` populate separate optional bodies without changing raw
  bytes. Preserve parser limits and `RequestParseError` status handling (400/413/415).
  Middleware limits apply after transport buffering; receive limits live in
  `ServerOptions::transport` and the shared incremental HTTP parser.
- Enforce receive limits before growing owned buffers. Keep trailers separate from
  headers, preserve original HEAD framing on error paths, and let the serializer
  own Content-Length and bodyless-status behavior.
- Socket operations and deadline callbacks run on the connection strand. Timer
  cancellation must invalidate already-queued callbacks; phase deadlines must not
  reset when receiving individual fragments. Shutdown cancels pending timers.
- GET implicitly matches HEAD in registration order; automatic OPTIONS runs only
  after ordinary routing falls through, using the request's router snapshots.
- `json()`, `send()`, and `end()` finish the response body/status. Exceptions discard buffered
  output before error handling. Direct dispatch uses the same application error
  handling as sockets. Preserve the original request method for HEAD wire framing.
- Coroutine routes use `Task<>`/Asio awaitables and explicit `*Async` registration.
  Preserve callback identity and snapshot/request/response lifetimes across waits.
  Sync middleware unwinds before starting a selected coroutine; finishing during
  that unwind is an error and cancels selection. `Next` never crosses suspension.
- Handlers execute on I/O workers and may run concurrently across connections.
  Coroutine continuations remain on the connection strand but may switch threads.
  Shared application state needs synchronization; never hold thread-owned locks
  across suspension. Configuration can change while serving requests.
- Handler deadlines close the connection and emit terminal cancellation. Errors
  after awaits use the snapshotted router error chains, except request cancellation.
  Preserve HEAD framing and serialize pipelined responses in arrival order.
- Stop joins executing workers, closes/cancels connections, drains ready cleanup,
  then destroys remaining I/O-context operations. Keep logger/dispatcher/request
  state alive through frame destruction. Do not resume normal transport after close.
- Server destruction occurs on its owning thread after handlers can return.
  Preserve shutdown behavior for idle connections and pending writes.

Socket tests should use loopback, dynamically allocated ports (`HttpServer`
with port `0`), and bounded waits. Follow the existing raw TCP test helpers for
framing, fragmentation, pipelining, and shutdown regressions.
For application tests, `lightning::testing::Client` provides socket-free requests
through the parser/dispatcher/serializer with owned results. Its referenced
App/Router must outlive the client; malformed input throws before dispatch. It
uses `dispatchAsync()` and a local blocking event loop, without transport deadlines.

## Change discipline

Inspect `git status` before editing and preserve existing staged and unstaged
work. Keep changes focused on the requested task. Do not commit build output,
Conan/ccache directories, or generated documentation. Treat bundled Doxygen
theme assets and toolchain files as third-party support code; edit only when
the task requires it. Update README usage guidance when public behavior or
build commands change.
