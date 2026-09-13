# Repository guidance

## Project overview

Lightning++ is a C++20 HTTP web framework in the `lightning` namespace. The
current implementation builds the `lightning` static library. Check source and
tests before assuming TODO features are implemented.

## Repository map

- `src/include/lightning/`: public headers and API types.
- `src/lib/`: `.cxx` implementations and the private `http_request_parser.h`.
- `src/test/`: GoogleTest tests for headers, parsing, responses, and socket behavior.
- `CMakeLists.txt`, `src/*/CMakeLists.txt`: dependencies, library, and test targets.
- `cmake/`: compiler flags, Conan integration, versioning, and Doxygen setup.
- `conanfile.txt`: pinned Asio, llhttp, GoogleTest, and fmt dependencies.
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

CMake registers one CTest test, `test_lightning`, with a 60-second timeout.
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
- `send()` and `end()` finish the response body/status. Exceptions discard buffered
  output before error handling. Direct dispatch uses the same application error
  handling as sockets. Preserve the original request method for HEAD wire framing.
- Handlers run synchronously on I/O workers and may execute concurrently across
  connections. Shared application state needs synchronization, and route/default
  handler configuration can change while serving requests.
- Server destruction occurs on its owning thread after handlers can return.
  Preserve shutdown behavior for idle connections and pending writes.

Socket tests should use loopback, dynamically allocated ports (`HttpServer`
with port `0`), and bounded waits. Follow the existing raw TCP test helpers for
framing, fragmentation, pipelining, and shutdown regressions.

## Change discipline

Inspect `git status` before editing and preserve existing staged and unstaged
work. Keep changes focused on the requested task. Do not commit build output,
Conan/ccache directories, or generated documentation. Treat bundled Doxygen
theme assets and toolchain files as third-party support code; edit only when
the task requires it. Update README usage guidance when public behavior or
build commands change.
