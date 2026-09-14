# Coroutine route stage validation

Validated on macOS arm64 with AppleClang 21.0.0 on September 13, 2026.
The suite contains 272 GoogleTests, including 24 new coroutine tests.

These commands passed:

```bash
./build.sh release test
./build.sh test asan=on ubsan=on
DEFAULT_BUILD_DIR=build/coverage ./build.sh coverage=on test
```

Release compilation treats warnings as errors. The installed-package consumer
now awaits an Asio operation inside a mounted JSON coroutine route, using only
the exported `lightning::lightning` target. The install/relocation and library-only
checks pass. The sanitizer build also compiles `lightning_async` and the four
existing examples. ASan/UBSan reported no memory or undefined-behavior errors.

| Metric | Covered / total | Coverage | Required |
| --- | --- | --- | --- |
| Lines | 1702 / 1765 | 96.43% | 90% |
| Branches | 752 / 820 | 91.71% | 85% |
| Functions | 232 / 247 | 93.93% | Reported only |

Coverage includes instrumented project sources and public headers, excluding
tests and dependencies. Reports are in `build/coverage/debug/coverage/`.

## Regression coverage

- All App/Router asynchronous method helpers, terminal empty completion, and
  rejection of invalid registration or empty tasks.
- Compile-time rejection of coroutine callbacks through synchronous route and
  middleware registration, where return-type erasure would otherwise drop work.
- JSON middleware, queries, nested mount context, decoded parameters, mutable
  callback identity, retained `Next` expiry, and middleware short-circuiting.
- Defined middleware unwinding before coroutine execution, including exceptions
  or attempted response completion after selecting an asynchronous route.
- Exceptions before and after awaiting, after buffering a response, and while
  creating a task; nested/local/parent error chains, replacement errors, restored
  mount context, and preservation of current local context on failure.
- Snapshot and callback lifetime after dispatcher destruction, registration
  changes during a request, and direct-dispatch validation.
- A single worker serves a fast request while another request is suspended.
- Ordered pipelined responses, original HEAD framing on success and error paths,
  and automatic OPTIONS method discovery.
- Handler timeout cancellation, frame-local destruction, no error response on
  cancellation, stop/restart with suspended work, and destruction when a handler
  disables cancellation. Shutdown does not wait for the test's ten-second timer.
- Request lifetime after peer disconnect, valid client send-half closure, and
  zero/negative handler deadline behavior.

## ThreadSanitizer limitation

A full run completed all 272 test assertions but failed with the previously
documented Asio/macOS report:

```bash
DEFAULT_BUILD_DIR=build/app-tsan ./build.sh test tsan=on
```

It reported one race in `conditionally_enabled_mutex.hpp:52`, reached from
`kqueue_reactor::run`, against accepted-socket descriptor registration. It
occurred in the existing
`TransportTimeouts.negative_values_are_rejected_before_startup_and_zero_disables`
test. No additional race was reported in the coroutine tests. This is not a
clean TSan result. The log is `build/async-tsan.log`; the independent reproducer
and earlier analysis remain in [transport validation](transport-validation.md).
No sanitizer suppression or dependency/backend change was added.

GitHub-hosted Linux/macOS CI was not executed in this session. The existing CI
matrix will run the expanded suite and coverage gate on push/pull request.

## Scope

Coroutine route handlers complete this roadmap stage. Middleware and error
handlers remain synchronous, with an explicit boundary before coroutine
execution. Cancellation is cooperative; disconnects are observed on subsequent
transport I/O, and direct/testing dispatch does not impose transport deadlines.
The README documents these contracts. Performance measurement remains a separate
workstream in [the performance plan](performance-plan.md).
