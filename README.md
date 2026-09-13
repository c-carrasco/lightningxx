Lightning++
===========

[![ci](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml/badge.svg)](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml)

## Introduction

Lightning++ is a lightweight, expressive, and flexible web framework for C++. Designed to facilitate the rapid development of web applications and APIs, Lightning++ offers a streamlined approach to handling HTTP requests and responses, middleware integration, and routing with a focus on high performance and minimal overhead.

## Features

- **Expressive Routing**: Method helpers, named parameters, final wildcards, and reusable routers with nested mounts.
- **Middleware Support**: Ordered application, path-prefix, and route middleware with request-local context and centralized error handling.
- **REST Helpers**: Decoded query parameters, opt-in JSON/form parsing, and JSON responses.
- **Fast and Lightweight**: Optimized for speed and a low memory footprint to deliver high performance.
- **Asynchronous Networking**: Nonblocking socket I/O with synchronous or C++20 coroutine route handlers on configurable I/O workers.
- **C++20 Library**: A compiled static library built with CMake and Conan.

## Quick Start

```cpp
#include <lightning/app.h>

int main() {
  lightning::App app;

  app.get ("/", [] (const auto &, auto &res) {
    res.send ("Hello World!");
  });

  app.listen (3000);
}
```

Constructing `App` opens no sockets. Configure routes first, then use `listen()`
to start the server and block, or `start()` to return after startup. The default
listening address is `127.0.0.1`. Use `ServerOptions` to set a numeric IPv4/IPv6
address, port, worker count, and log level:

```cpp
lightning::ServerOptions options;
options.address = "127.0.0.1";
options.port = 3000; // Use zero to let the OS allocate a port; read app.port().
options.workers = 4;
app.start (options);
// The application can do other work here.
app.stop();
```

`stop()` waits for currently executing callbacks, closes connections, and cancels
suspended coroutine handlers. Call it from a
control thread, never from a request handler. Calling it again is harmless.
For blocking `listen()`, another control thread calls `stop()`; join the thread
running `listen()` before destroying the app. Lifecycle methods are ordinary
thread APIs and must not be called directly from an OS signal handler.
An app can restart after stopping, retaining its routes. Starting an already
running/stopping app throws `std::logic_error`; failed startup leaves it reusable.
`running()` is false and `port()` is zero while stopped or stopping.

Available helpers are `get`, `head`, `post`, `put`, `del`, `connect`, `options`,
`trace`, and `patch`. `del` represents HTTP DELETE because `delete` is a C++
keyword. They return `App &` for chaining; `addRoute()` remains available for
explicit `HttpMethod` registration.

Routes match the method and path pattern in registration order. Query strings
are separate from the path; case and trailing slashes are significant. Register
HEAD before GET to override its implicit HEAD handling. `setDefault()` installs a fallback; an empty
handler restores 404 for an application or parent fallthrough for a mounted router.

To build and run the [hello/echo example](examples/hello.cxx):

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DLIGHTNING_BUILD_EXAMPLES=ON
cmake --build build/debug --target lightning_hello --parallel
./build/debug/bin/lightning_hello
# From another terminal:
curl http://127.0.0.1:3000/
curl --data-binary 'hello' http://127.0.0.1:3000/echo
```

Use `App::dispatch(request, response)` for application tests without opening a
socket. It shares the `Dispatcher` and error handling used by `HttpServer`.
Unhandled callback errors produce a finished 500 response with `shouldClose()`
set to true. This replaces the earlier direct-dispatch behavior of propagating
callback exceptions. It does not perform HTTP parsing or wire-level HEAD body
suppression. Mutable requests retain middleware changes; const requests are copied.
Always supply a fresh, unfinished response to dispatch.

## Middleware and errors

Middleware takes `(HttpRequest &, HttpResponse &, Next)`. Register it globally
with `app.use(handler)`, at a path prefix with `app.use("/api", handler)`, or
alongside a route's terminal handler:

```cpp
app.use ("/api", [] (auto &req, auto &res, lightning::Next next) {
  req.locals["source"] = std::string ("example");
  res.headers().set ("x-framework", "lightning");
  next();
});

const lightning::Middleware requireBody = [] (auto &req, auto &, auto next) {
  if (req.body.empty())
    throw std::invalid_argument ("Body required");
  next();
};

app.post ("/api/echo", requireBody, [] (const auto &req, auto &res) {
  res.send (std::string (req.body.begin(), req.body.end()));
});

app.onError ([] (std::exception_ptr error, auto &, auto &res, auto next) {
  try {
    std::rethrow_exception (error);
  }
  catch (const std::invalid_argument &) {
    res.status (400).send ("Body required");
  }
  catch (...) {
    next (error);
  }
});
```

The complete [middleware example](examples/middleware.cxx) builds as
`lightning_middleware` when `LIGHTNING_BUILD_EXAMPLES=ON`.

Middleware and routes execute in registration order. For synchronous routes,
`next()` runs the remaining chain, then returns so middleware can perform cleanup
or logging. Coroutine routes have an explicit suspension boundary described below.
`use("/api", ...)` matches `/api` and `/api/...`, but not `/apiculture`. A trailing
slash on the registered prefix is ignored. Prefix matching is case-sensitive;
literal segments are compared without decoding. Prefixes can contain named
parameters. Middleware does not strip the prefix from `req.path`; router mounts do.

Three-argument callbacks must call `next()` or finish the response. Returning
without either is a programming error and enters the error chain. `send()` and
`end()` finish the body/status; sending again or changing status then throws
`std::logic_error`. Headers can still be decorated while middleware unwinds,
before the buffered response is serialized. Existing two-argument route/default
handlers are terminal: returning finishes an empty response if needed. A route
chain that calls through its last callback proceeds to later matching layers;
exhausting all layers invokes the configured fallback or returns 404.

`Next` is single-use, confined to its callback thread, and expires when that
callback returns or throws. Copies share that state. Repeated, expired, and
cross-thread calls throw `std::logic_error`; retained continuations cannot perform
deferred work. Middleware and error handlers remain synchronous.

`req.locals` stores per-request values as `std::any`; read them with
`std::any_cast<T>(req.locals.at("key"))`. Parsed body bytes remain available in
`req.body`. Parsing a new request clears locals. Callback objects are retained
across requests, so synchronize shared captures when using multiple workers.
Each dispatch snapshots registrations: callbacks added during a request affect
subsequent requests, without a configuration lock being held around user code.

`onError()` registers a separate, ordered error chain for exceptions and
`next(std::exception_ptr)` from middleware, routes, or the default handler.
Buffered response data is discarded before entering this chain; request locals
remain available. Each error handler can send a response or forward with
`next(error)`; `next()` forwards the current error. Error handling never resumes
normal routing. Each error handler runs at most once in an active dispatcher, and exceptions
from an error handler advance to the remaining handlers. Registration order
relative to ordinary routes does not affect this separate error chain.

Handled errors can keep the connection alive. Unhandled application errors receive
a generic 500 and close the connection; route decoding errors default to 400.
Request helper parsing errors default to 400, 413, or 415 as described below.
A normal 404 does not enter the error chain; invalid
HTTP parsing still receives 400 before application dispatch. Because responses
are buffered, exceptions thrown after `send()` also discard that buffered output
and enter error handling; only the final response is written to the socket.

## Route parameters and routers

```cpp
lightning::Router users;
users.get ("/:id", [] (const auto &req, auto &res) {
  res.send ("User " + req.params.at ("id"));
});

app.use ("/api/users", users);
// GET /api/users/alice -> "User alice"
```

The [nested-router example](examples/router.cxx) builds as `lightning_router`
when `LIGHTNING_BUILD_EXAMPLES=ON`. Routers offer the same method helpers,
middleware, default handlers, and error handlers as `App`, and can mount other
routers with `router.use(prefix, child)`.

Patterns are compiled and validated when registered:

- `:name` captures one nonempty path segment, such as `/users/:id`.
- A final `*name` captures a nonempty remainder, including slashes, such as
  `/files/*path`. The captured value is one string. `/files/` does not match it.
- Names use ASCII letters or `_` followed by letters, digits, or `_`. Each name
  must occupy a whole segment and be unique within that pattern.
- Routes are absolute paths. Literal `*` is also supported for an OPTIONS
  asterisk-form request. Optional segments, regular expressions, and embedded
  parameters such as `/file-:id` are not supported. Invalid patterns throw
  `std::invalid_argument` before registration changes.
- Literal segments match raw bytes. Encode reserved literal characters when
  needed, for example `%3A` for a literal colon. Duplicate slashes are significant.
- Registration order decides precedence, including literal/parameter/wildcard
  overlaps. There is no automatic preference for literal routes.

`req.params` owns the decoded strings. Matching splits the raw path before
decoding captures, so `%2F` can become `/` inside a value without introducing a
new route segment. Decoding happens exactly once; `%252F` becomes `%2F`, and
`+` remains `+`. Decoded bytes are preserved without Unicode normalization.
Malformed percent escapes and null bytes in a matched capture raise
`RouteDecodeError`, which enters `onError()` and defaults to a 400 response with
connection closure. An unrelated, nonmatching route does not decode partial
captures. Literal segments are not decoded or validated as captures.

Mount prefixes support literal and named segments; wildcards are limited to
complete routes. Inside a router, `req.path` is relative to the mount and
`req.baseUrl` contains the raw matched prefix. `req.url` and `req.query` stay
unchanged. Both `/api` and `/api/` enter a router mounted at `/api` with a relative
path of `/`. A root mount consumes no path segments.

Parent mount parameters are inherited by children. Child names override matching
parent names while the child is active. Route and middleware captures are scoped
to their layer and its callbacks; after `next()` they are restored for that
callback's remaining work. Parent path, base URL, and parameters are restored
when leaving a router, including during parent fallthrough and error handling.
Use `req.locals` to pass values across sibling layers. Direct dispatch clears
initial routing context; parsing another request clears `params` and `baseUrl`.

An unmatched router, or one whose last callback calls `next()`, continues to the
next parent layer. A router's explicit `setDefault()` is terminal instead.
Router-local error handlers run before errors propagate to the parent's error
chain; errors handled locally do not reach the parent. Errors in a mount pattern
are handled by its parent because the child has not been entered yet.

Router copies and mounts share live configuration. Mounts retain the router's
lifetime, and later registrations are visible on subsequent requests. All
reachable routers are snapshotted before the first callback runs, with one
snapshot per shared router per request. Concurrent configuration is supported;
updates across separate routers are not one atomic transaction. Cyclic mounts,
including indirect cycles, are rejected with `std::invalid_argument`.

## Query parameters, JSON, and forms

```cpp
#include <lightning/app.h>
#include <lightning/body_parser.h>

lightning::App app;
app.use (lightning::json());
app.use (lightning::urlencoded());

app.get ("/search", [] (const auto &req, auto &res) {
  const auto query = req.queryParams();
  res.json (query); // ?tag=a&tag=b -> {"tag":["a","b"]}
});

app.post ("/echo", [] (const auto &req, auto &res) {
  if (req.jsonBody) res.json (*req.jsonBody);
  else if (req.formBody) res.json (*req.formBody);
  else res.status (415).json ({ { "error", "Expected JSON or a form" } });
});
```

The [REST example](examples/rest.cxx) builds as `lightning_rest` with
`LIGHTNING_BUILD_EXAMPLES=ON`. It mounts these handlers under `/api` and adds
JSON error responses:

```bash
./build/debug/bin/lightning_rest
# From another terminal:
curl 'http://127.0.0.1:3000/api/search?tag=a&tag=b'
curl -H 'Content-Type: application/json' -d '{"name":"Ann"}' http://127.0.0.1:3000/api/echo
curl -d 'name=Ann+Lee&tag=a&tag=b' http://127.0.0.1:3000/api/echo
```

`req.query` remains the raw query string. `queryParams()` parses its current
contents on each call and returns an owned `UrlParameters`: a map from strings
to vectors of strings. Repeated values keep their arrival order. Query and form
parsing share `parseUrlEncoded(input, options)`:

- Split on `&` and the first `=` before percent-decoding once; `+` becomes space.
- A missing `=` means an empty value. Empty names are allowed; empty fields
  between ampersands are ignored. Semicolons are ordinary value bytes.
- Names stay flat: `user[name]` and `items[]` are literal keys. Decoded duplicate
  names are combined. Byte strings, including null bytes, are preserved without
  UTF-8 validation or normalization.
- Malformed percent escapes throw `RequestParseError` (400). Defaults allow
  100 KiB of raw input and 1,000 nonempty fields, counting repeated keys.
  Exceeding either limit throws 413. For example,
  `req.queryParams({ .limit = 8192, .parameterLimit = 100 })` sets smaller limits.

Body parsing is opt-in middleware; it runs in registration order and can be
mounted globally, on a router, or on one route. The parsers preserve `req.body`
and populate separate owned optionals, `req.jsonBody` and `req.formBody`.
Each invocation clears its own optional and parses the current body, so repeated
registrations observe intervening body changes. Parsing a new HTTP request clears
both optionals. Missing or unrelated Content-Type values pass through without
parsing; applications decide whether their endpoint requires a parsed body.

`json()` matches `application/json` and `application/<subtype>+json`.
It accepts all JSON values, including scalars and null, validates UTF-8, accepts
an initial UTF-8 BOM, and rejects malformed syntax, trailing documents, comments,
raw null bytes, and overflowing numbers. Duplicate object keys retain the last
value. An empty body leaves `jsonBody` unset; whitespace-only input is invalid.
An engaged optional containing JSON null is distinct from an absent body.

`urlencoded()` matches `application/x-www-form-urlencoded`, using the same flat
rules as query parsing. An empty matching body produces an engaged, empty map.
Media types and parameter names are case-insensitive. Both parsers accept an
absent charset or `charset=utf-8`, including quoted values. Unsupported charsets
or Content-Encoding values other than `identity` produce 415; automatic
charset conversion and decompression are not provided.

```cpp
app.use (lightning::json ({ .limit = 256 * 1024, .maxDepth = 64 }));
app.use (lightning::urlencoded ({ .limit = 64 * 1024, .parameterLimit = 200 }));
```

JSON defaults to 100 KiB and 128 nested containers; `maxDepth` must be positive.
Form defaults match query parsing. Limits are inclusive and apply to actual
body bytes, including assembled chunked bodies. These are parsing limits after
the HTTP transport has buffered the request. Configure the separate transport
limits below to bound requests as they arrive.

`RequestParseError::status()` reports 400 for malformed data, 413 for exceeded
limits, or 415 for unsupported encoding. These errors enter `onError()`, including
errors from `queryParams()` called inside a handler. Unhandled parsing errors
return a generic message and close the connection. Custom error handlers may
return JSON and keep the connection alive. Application errors such as accessing
an absent JSON member still default to 500.

`lightning::Json` aliases `nlohmann::json`, pinned to version 3.11.3 through Conan.
`res.status(201).json(value)` serializes an owned response, sets
`application/json; charset=utf-8`, and finishes it. Strings are JSON-escaped;
they are never treated as pre-serialized JSON. Use `Json::array()` for explicit
arrays. Invalid UTF-8 during serialization throws before changing the response;
inside dispatch, an unhandled serialization error becomes 500. The existing
response completion rules and HEAD wire framing also apply to `json()`.

## Transport limits and HTTP behavior

```cpp
using namespace std::chrono_literals;
lightning::ServerOptions options;
options.port = 3000;
options.transport.limits.headerBytes = 16 * 1024;
options.transport.limits.targetBytes = 8 * 1024;
options.transport.limits.headerCount = 100;
options.transport.limits.bodyBytes = 1024 * 1024;
options.transport.headerTimeout = 10s;
options.transport.bodyTimeout = 30s;
options.transport.writeTimeout = 30s;
options.transport.keepAliveTimeout = 5s;
options.transport.handlerTimeout = 30s;
app.start (options);
```

These values are the defaults. Limits are inclusive and per request:

| Limit | What is counted | Rejection |
| --- | --- | --- |
| `headerBytes` | Raw initial request line and headers, including spaces and delimiters; also a shared storage budget for header/trailer names and values | 431 |
| `targetBytes` | Raw request target, including the query string | 414 |
| `headerCount` | Header and trailer fields, counting duplicates | 431 |
| `bodyBytes` | Actual body bytes after HTTP chunk framing is removed | 413 |

Declared Content-Length and chunk sizes are checked before allocating body data;
fragmented body data is checked before appending. Header and target limits also
bound parsing before appending their data. Coalesced body bytes and subsequent
pipelined requests do not consume the current request's initial-header budget.
Zero is a literal size/count limit. Raw body limits apply even without body-parser
middleware. `HttpRequest::parse(input, limits)` accepts the same `RequestLimits`;
its boolean result remains false for rejected input.

Timeouts are absolute deadlines for each phase, not inactivity timers reset by
individual fragments. A new connection starts the header deadline. After headers
complete, an incomplete body gets its own deadline. Each response write gets a
write deadline; a completed response starts the keep-alive idle deadline, and the
next request starts a fresh header deadline. Zero disables that timeout; negative
values are rejected before startup. Expiration closes the connection without
sending another response. Timer callbacks and socket callbacks share the
connection's strand, and canceled timers cannot close later request phases.

Timeouts do not interrupt executing application code, and blocked I/O workers can
delay timer execution. The handler deadline includes coroutine suspension and
requests terminal cancellation when it expires. These limits bound individual
requests; they do not impose a total connection count or application response-size
limit.

Initial HTTP lines require CRLF. Duplicate ordinary request fields are combined
with commas (`Cookie` uses semicolons); duplicate Host and Content-Length fields
are rejected. Trailers are stored in `req.trailers`, separately from `req.headers`,
and cannot replace framing, routing, authentication, or body-interpretation fields.
Transport parse/limit errors bypass application middleware, return an error
response, and close the connection without processing the remaining pipeline.
Unsupported HTTP versions receive 505 when recognized; malformed input receives
400. `Expect` is currently rejected with 417, including `100-continue`; interim
responses are not implemented.

The buffered response serializer controls Content-Length from the actual body
and omits application-supplied Transfer-Encoding and Trailer fields. It suppresses
bodies for HEAD, 204, 205, and 304; 204/304 omit Content-Length, while 205 sends zero.
HEAD keeps the length of the corresponding buffered representation. Only final
statuses 200–599 are accepted. Header names and values are validated when set
and again when serialized to prevent response splitting, including mutations
through header iterators. A response `Connection: close` token closes the socket;
HTTP/1.0 keep-alive responses explicitly advertise persistence.

GET routes also match HEAD while retaining `req.method == HttpMethod::kHead`.
Registration order still applies: register an explicit HEAD handler before the
GET handler to override it. Direct dispatch retains the buffered body; suppression
happens when serializing the socket response.

When OPTIONS falls through middleware, routers, and explicit handlers without a
custom fallback, matching registered routes produce a 204 with an `Allow` header.
GET implies HEAD, and OPTIONS is included. Nested and shared routers contribute
methods from the same request snapshot. OPTIONS `*` lists methods throughout the
registered router graph. No matching route yields 404. A custom fallback or
explicit OPTIONS handler can override the automatic response. Other unmatched
methods retain 404; automatic 405 responses are not enabled.

See [transport validation](doc/transport-validation.md) for test results and the
standalone reproducer for the unresolved Asio/macOS ThreadSanitizer report.

## Documentation

See the [performance measurement plan](doc/performance-plan.md) for proposed
benchmarks, metrics, and initial performance targets.

The examples above document the current API. Generate the API reference with
`./build.sh release doc` (Doxygen required); output is in `build/release/doc/html`.
See [packaging and coverage validation](doc/packaging-validation.md) for the
distribution and test tooling checks, and [coroutine validation](doc/async-validation.md)
for the coroutine route stage.

## Prerequisites

Before diving into `Lightning++`, make sure you have the following tools and dependencies set up:

- Conan 1.63+ or Conan 2.x (recommended)
- CMake 3.20 or higher
- GCC 13+, Clang 17+, or AppleClang 15+
- GNU Make or Ninja
- Docker (optional)
- Doxygen (for generating documentation)

## Build the Project

### Achieve Reproducible Builds with Docker

For a streamlined development environment, Docker is your friend. It's highly recommended to utilize Docker for building your code.

### The Build Script

The `build.sh` script is located in the project's root directory.

Run `./build.sh` for a debug build, or `./build.sh test` to build and run the tests.
The first build downloads dependencies and creates a default Conan profile if one
does not exist. An existing default profile is preserved. Dependency installation
uses [CMakeDeps](https://docs.conan.io/2/reference/tools/cmake/cmakedeps.html), which
supports the CMake package configuration files used by this project.

### Usage Guide

- `release`: Build the code in release mode.
- `debug`: Build the code in debug mode (selected if no mode is specified).
- `clean`: Remove the build directory and its contents.
- `verbose`: Show the commands executed by GNU Make.
- `ninja`: Use Ninja instead of GNU Make for compilation.
- `tests`: Run tests after compilation.
- `asan=on`: Enable AddressSanitizer.
- `ubsan=on`: Enable UndefinedBehaviorSanitizer.
- `tsan=on`: Enable ThreadSanitizer.
- `coverage=on`: Run LLVM coverage and enforce project line/branch floors (Debug Clang).
- `docker[=compiler]`: Use Docker for local development. Available compilers:
  - `gcc13`: Use GCC 13 (selected if no compiler is specified).
  - `clang17`: Use Clang 17.

Sanitizers default to off on each invocation. Enable them explicitly when needed;
`tsan=off` also disables ThreadSanitizer. Run ThreadSanitizer separately from
AddressSanitizer. `tsan=on` enables instrumentation in both Debug and Release builds.

Examples:

```bash
# Compile code in release mode
./build.sh clean release

# Build in debug mode with Clang 17 and AddressSanitizer, then run unit tests
./build.sh docker=clang17 debug test asan=on

# Build and run tests with ThreadSanitizer
./build.sh test tsan=on

# Start a Docker development environment with GCC 13
./build.sh docker=gcc13
```

## How to Use It

Request headers and bodies own their data. `HttpRequest::body` is a
`std::vector<uint8_t>` containing the actual bytes, including embedded nulls.
`HttpRequest::parse()` accepts exactly one complete request and returns `false`
for incomplete or invalid input. The server handles incremental parsing and
pipelining internally.

Route paths and response header values are copied, so temporary strings are
safe to pass. Responses default to status 200. Invalid requests receive 400;
unhandled callback errors receive 500 and close that connection. Use `onError()`
to customize application error responses.

Routes and the default handler can be changed while the server is running.
Handlers run on I/O workers and can execute concurrently for different
connections; coroutine handlers can suspend between operations. Synchronize
any application state they share. Destroy
the server from its owning thread after running handlers can return. Shutdown
closes idle connections and pending writes.

Run `./build.sh test asan=on ubsan=on` for the regression suite with memory and
undefined-behavior checks. The socket tests use dynamically allocated local
ports and bounded waits and cover fragmented and pipelined requests, binary
and chunked bodies, ownership, handler exceptions, concurrent configuration,
and shutdown.

## Installation

Build and install a Release package into a prefix of your choice:

```bash
./build.sh release test
cmake --install build/release --prefix "$PWD/build/install"
```

Consumers use the exported CMake target, which supplies C++20 and transitive
include/link requirements:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES CXX)
find_package(lightning 0.1 CONFIG REQUIRED)
add_executable(my_app main.cxx)
target_link_libraries(my_app PRIVATE lightning::lightning)
```

The install contains the static archive, public headers, CMake configuration,
and pinned CxxLogger headers/license. Asio 1.29.0, llhttp 9.1.3, fmt 10.0.0, and
nlohmann/json 3.11.3 remain external dependencies, discovered through their CMake
config packages. Exact versions preserve the tested public-header/ABI combination;
use the same architecture, compiler, standard library, and build configuration
as the archive. GoogleTest is not an installed-package dependency. Installed
consumers do not run Conan or fetch CxxLogger during CMake configuration.

For a local smoke test, the dependency configurations already generated in
`build/release` can be reused:

```bash
cmake -S test/consumer -B build/consumer -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/install;$PWD/build/release"
cmake --build build/consumer
ctest --test-dir build/consumer --output-on-failure
```

On another machine, provision those dependencies first and place their CMake
configurations on `CMAKE_PREFIX_PATH` alongside the installation prefix. The
[consumer Conan recipe](test/consumer/conanfile.txt) lists only runtime/public
dependencies; run `conan install` with a matching host profile and
`-s build_type=Release -s compiler.cppstd=20 --build=missing --output-folder=...`.
The Lightning++ prefix can be moved after installation; external dependency
locations are resolved on the consumer machine, not embedded in our export.

Direct source builds support `-DBUILD_TESTING=OFF` to skip test targets and
GoogleTest discovery, and `-DLIGHTNING_BUILD_EXAMPLES=ON` to build the examples.
The default Conan recipe still provisions GoogleTest; use
`-DLIGHTNING_USE_CONAN=OFF` with dependencies already on `CMAKE_PREFIX_PATH`
to skip automatic dependency installation entirely. Source builds fetch the
pinned logger unless `FETCHCONTENT_SOURCE_DIR_CXXLOGGER` supplies its checkout.
Install ordinary Release builds for distribution; sanitizer and coverage runtime
flags are not exported to consumers.

## Testing applications without sockets

```cpp
#include <lightning/app.h>
#include <lightning/testing.h>

lightning::App app;
app.get ("/users/:id", [] (const auto &req, auto &res) {
  res.json ({ { "id", req.params.at ("id") } });
});
lightning::testing::Client client { app };
const auto result = client.get ("/users/42");
// Assert using any test framework:
// result.status == 200
// result.json().at("id") == "42"
// result.headers.get("content-type") == "application/json; charset=utf-8"
```

The client accepts an `App`, `Router`, or `Dispatcher` that outlives it.
`get(target, headers)`, `post(target, body, headers)`, and
`request(method, target, body, headers)` build and parse a complete HTTP request.
Headers default to empty; Host defaults to `localhost`, and Content-Length is
computed from the body bytes. Pass `HttpHeader` with Content-Type to test JSON
or form middleware. Binary bodies and returned results own their data.

`inject(rawHttp)` accepts one complete raw request, including chunked requests
and trailers. Invalid, incomplete, multiple, or over-limit requests throw
`std::invalid_argument` before dispatch. Override receive limits with
`Client { app, RequestLimits { .bodyBytes = 4096 } }`. Application errors still
enter the application's error chain. Responses contain the serialized status,
headers, and body, including Content-Length and HEAD/bodyless-status suppression;
`result.json()` parses the body and throws for invalid JSON. The client does not
simulate sockets, deadlines, peer addresses, or connection persistence. Keep
socket integration tests for transport behavior.

## Coroutine route handlers

```cpp
#include <chrono>
#include <asio/steady_timer.hpp>
#include <lightning/app.h>

using namespace std::chrono_literals;
lightning::App app;
app.getAsync ("/hello/:name", [] (auto &req, auto &res) -> lightning::Task<> {
  asio::steady_timer timer { co_await asio::this_coro::executor, 100ms };
  co_await timer.async_wait (asio::use_awaitable);
  res.json ({ { "hello", req.params.at ("name") } });
});
app.get ("/health", [] (const auto &, auto &res) { res.send ("ok"); });
app.listen (3000);
```

The [coroutine example](examples/async.cxx) builds as `lightning_async` with
`LIGHTNING_BUILD_EXAMPLES=ON`. Even with one worker, `/health` can respond while
`/hello/...` waits. Blocking calls, CPU-heavy loops, and `std::this_thread::sleep_for`
still occupy their worker; use asynchronous operations for waits.

`Task<T>` aliases `asio::awaitable<T>`, with `void` as the default. Use Asio's
`use_awaitable` completion token and obtain the request executor through
`co_await asio::this_coro::executor`. Construct per-request timers and I/O objects
on that executor. Execution is serialized on the connection's strand, but can
resume on a different worker thread. Do not hold a thread-owned lock across a
suspension or assume thread-local state follows a request.

`App` and `Router` provide `getAsync`, `headAsync`, `postAsync`, `putAsync`,
`delAsync`, `connectAsync`, `optionsAsync`, `traceAsync`, and `patchAsync`.
`addAsyncRoute(method, path, handler)` is also available on `Dispatcher` and
`HttpServer`. A coroutine route is one terminal handler taking `HttpRequest &`
(or a const reference) and `HttpResponse &`, returning `Task<>`. It has no `Next`
argument. Normal return finishes an empty response if necessary; `send()`,
`json()`, and `end()` buffer the result until the coroutine completes. Empty
tasks and exceptions while creating a task enter the application error chain.

Use the explicitly asynchronous registration functions. Passing a coroutine
callback directly to a synchronous route helper, default handler, middleware,
or error-handler registration is rejected, preventing its return value from
being silently discarded. Do not manually erase an asynchronous callback into
a synchronous `std::function<void(...)>`.

### Middleware, routing, and errors

Synchronous middleware runs through route selection and unwinds **before the
selected coroutine starts**, even if that coroutine never suspends. Code after
`next()` runs before the coroutine's response exists. It can decorate headers,
but cannot finish the response after selecting a coroutine route; doing so enters
`onError()` and discards the pending route. Middleware can short-circuit normally
by finishing without calling `next()`. An exception during unwinding also
discards the pending coroutine. Use code in the coroutine, including local RAII
objects, for work that must span its execution. Asynchronous middleware and
error handlers are not part of this stage, and `Next` still expires synchronously.

Coroutine routes use the same registration order, route parameters, nested
routers, parser middleware, HEAD fallback, automatic OPTIONS, and buffered
response framing. The request, response, callback object, routing context, and
configuration snapshots remain alive during suspension. Registration changes
affect subsequent requests. Pipelined requests on one connection are processed
in order, with the next request dispatched after the previous response is written.

Exceptions before or after suspension—including after buffering a response—
discard the response and enter the existing `onError()` chain. Router-local
handlers run before parent handlers; propagation restores each parent's routing
context. Unhandled errors retain the existing 400/413/415/500 behavior. Request
cancellation bypasses application error handling and closes the connection.

### Deadlines, disconnects, and shutdown

`ServerOptions::transport.handlerTimeout` defaults to 30 seconds, starting before
dispatch and including all coroutine waits. Zero disables it; negative values
are rejected. Expiration closes the connection and requests terminal cancellation
of the active coroutine. Awaited operations must support cancellation for prompt
cleanup. Catching cancellation must not turn it into an endless retry loop;
disabling cancellation can retain the suspended handler until its operation ends
or the server stops. No deadline can interrupt blocking application code.

A peer disconnect is detected on subsequent transport I/O, so it does not
necessarily cancel a suspended handler immediately. The handler deadline bounds
that wait. A client closing its sending half is still allowed to receive a
response. Request references remain valid through completion; never pass them
to detached work that outlives the handler.

`stop()` joins executing workers, closes connections, requests terminal
cancellation, and drains ready cancellation completions on the control thread.
Remaining operations owned by the server's I/O context are destroyed with that
context, releasing suspended frames and their local objects. Keep awaited work
on the request executor and avoid unbounded work during cancellation cleanup.
Call `stop()` from a control thread, never from middleware, a coroutine handler,
or its cleanup code. An app can then restart with the same registered routes.

### Direct dispatch and testing

`co_await app.dispatchAsync(req, res)` (also on `Router` and `Dispatcher`) supports
both route kinds. Snapshots are captured when `dispatchAsync()` is called; the
request and response must outlive the awaited operation. Direct dispatch uses
the caller's executor and cancellation state, without transport deadlines.
Synchronous `dispatch()` rejects a matched coroutine route through the error
chain; it does not run an event loop internally.

`lightning::testing::Client` supports coroutine routes by running a local event
loop and blocking until dispatch completes. Its parser limits still apply, but
it does not impose a handler timeout. Use finite asynchronous operations in
application tests and socket tests for cancellation/deadline behavior.

## Coverage and CI

Use a separate Debug Clang/AppleClang build with matching `llvm-cov`,
`llvm-profdata`, and Python 3.9+:

```bash
DEFAULT_BUILD_DIR=build/coverage ./build.sh coverage=on test
```

This runs the suite and coverage-tool tests, then generates fresh profiles from
a full unfiltered suite. Reports are in `build/coverage/debug/coverage/`:
`html/index.html`, `coverage.lcov`, and `summary.json`. Coverage counts project
code in `src/lib` and `src/include/lightning`, excluding tests and dependencies.
The command fails below **90% line coverage or 85% branch coverage**, on test
failure, or on missing project coverage data. CMake cache settings
`LIGHTNING_COVERAGE_MIN_LINES` and `LIGHTNING_COVERAGE_MIN_BRANCHES` configure the
floors. `cmake --build <coverage-build> --target coverage` regenerates the report.
Coverage and sanitizers require separate builds; ordinary GCC Debug builds no
longer enable coverage implicitly. Like sanitizers, `coverage=on` resets to off
on the next build-script invocation unless explicitly supplied.

CI runs on pushes and pull requests, with GCC/Clang Linux and AppleClang macOS
Release and ASan/UBSan checks. Ordinary builds also test library-only configuration
and compile/run a consumer against an installed, relocated package. A separate
LLVM coverage job enforces the floors and uploads HTML/LCOV/JSON reports.
Documentation deployment runs only after successful checks on a default-branch
push. The known Asio/macOS TSan report remains documented in
[transport validation](doc/transport-validation.md).

## Benchmarks

Run `./build.sh bench test` to build optional benchmarks in
`build/benchmarks/release` and validate all workloads. The suite includes 159
parser, routing, middleware, serialization, and coroutine cases, plus a real HTTP
server and a validated loopback load runner. Google Benchmark is fetched only for
benchmark builds. JSON results include repetitions and build/environment metadata.

See [benchmark commands and measurement rules](benchmarks/README.md) for timing
runs, HTTP profiles, and baseline comparisons. CI checks correctness without
timing thresholds; reference-machine capacity and soak testing remain in the
[performance plan](doc/performance-plan.md).

## Contributing

We welcome contributions from the community.

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for details.
