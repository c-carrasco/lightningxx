# REST helper validation

Validated on macOS arm64 with AppleClang 21.0.0 on September 13, 2026.
The suite contains 206 tests, including 49 new REST tests.

Passed:

```bash
./build.sh release test
./build.sh test asan=on ubsan=on
```

The Debug build also compiled all four examples with
`LIGHTNING_BUILD_EXAMPLES=ON`, including `lightning_rest`.

The full ThreadSanitizer run did **not** pass:

```bash
DEFAULT_BUILD_DIR=build/app-tsan ./build.sh test tsan=on
```

All 206 GoogleTest assertions passed, but ThreadSanitizer reported a race during
`MiddlewareSockets.authentication_and_handled_errors_preserve_pipelining_and_context_isolation`.
The report identifies a read in Asio 1.29's
`conditionally_enabled_mutex::scoped_lock` from `kqueue_reactor::run`, and a
write from `conditionally_enabled_mutex` construction during socket descriptor
registration. The CTest log is at
`build/app-tsan/debug/Testing/Temporary/LastTest.log`.
An isolated run of the 49 REST tests also completed its assertions but reported
a ThreadSanitizer warning with sockets enabled.

The 45 REST tests without sockets passed ThreadSanitizer, including concurrent
dispatch through shared JSON/form middleware:

```bash
build/app-tsan/debug/bin/test_lightning --gtest_filter='UrlParameters.*:RestQuery.*:JsonParser.*:BodyParsers.*:FormParser.*:JsonResponse.*:SyntaxAndEncoding/*'
```

Investigate the socket reactor report during the transport stage. It has not
been classified as a real race or a sanitizer false positive. No sanitizer
suppression or Asio dependency change was made for this stage.

The subsequent transport stage isolated the report in a standalone Asio program;
see [transport validation](transport-validation.md) for current results.
