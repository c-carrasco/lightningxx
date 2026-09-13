# Transport stage validation

Validated on macOS arm64 with AppleClang 21.0.0 on September 13, 2026.
The suite contains 241 tests, including 35 new transport/framing/method tests.

Both full-suite commands passed:

```bash
./build.sh release test
./build.sh test asan=on ubsan=on
```

The Debug build also compiled all four application examples. New coverage includes
receive-limit boundaries, every split point across representative requests,
chunk-size checks before allocation, trailers, ambiguous framing, header
injection, bodyless responses, nested HEAD/OPTIONS routing, absolute phase
deadlines, timer cancellation, stalled writes, and shutdown with pending timers.

The full ThreadSanitizer run completed all 241 test assertions but failed because
of one sanitizer report:

```bash
DEFAULT_BUILD_DIR=build/app-tsan ./build.sh test tsan=on
```

The report is a read of Asio's `conditionally_enabled_mutex::enabled_` from
`kqueue_reactor::run` concurrent with its initialization during accepted-socket
descriptor registration. It occurred during
`TransportTimeouts.negative_values_are_rejected_before_startup_and_zero_disables`.
No timer-state race was reported. The full log is in
`build/app-tsan/debug/Testing/Temporary/LastTest.log`.

## Standalone reproduction

[asio-kqueue-tsan.cxx](asio-kqueue-tsan.cxx) reproduces the same report without
including or linking Lightning++, its parser, middleware, timers, or GoogleTest.
It creates a three-worker Asio server, accepts a connection onto a strand, and
echoes one byte asynchronously. Delaying the client's write forces the server
through a pending kqueue read. An immediate-write version did not reproduce the
report in the diagnostic runs.

Compile using the include directory of the pinned Asio 1.29.0 package:

```bash
clang++ -std=c++20 -g -fsanitize=thread -pthread \
  -I /path/to/asio/include doc/asio-kqueue-tsan.cxx -o build/asio-kqueue-tsan
./build/asio-kqueue-tsan
```

The standalone run exited with status 134 and reported the same read in
`conditionally_enabled_mutex.hpp:52`, reached from `kqueue_reactor.ipp:486`.
The diagnostic log from this session is `build/tsan-kqueue-repro.log`.

This isolates the report to Asio/macOS behavior; it does not establish whether
the report represents a real race or missing sanitizer synchronization tracking.
No dependency patch, backend change, or sanitizer suppression was introduced.
Further investigation can use the standalone reproducer without the application
framework or HTTP tests.
