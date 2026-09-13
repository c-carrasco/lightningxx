Lightning++
===========

[![ci](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml/badge.svg)](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml)

## Introduction

Lightning++ is a lightweight, expressive, and flexible web framework for C++. Designed to facilitate the rapid development of web applications and APIs, Lightning++ offers a streamlined approach to handling HTTP requests and responses, middleware integration, and routing with a focus on high performance and minimal overhead.

## Features

- **Expressive Routing**: Register exact method/path routes using `app.get()`, `app.post()`, and other method helpers.
- **Middleware Support**: Ordered application, path-prefix, and route middleware with request-local context and centralized error handling.
- **Fast and Lightweight**: Optimized for speed and a low memory footprint to deliver high performance.
- **Asynchronous Networking**: Nonblocking socket I/O with synchronous handlers running on configurable I/O workers.
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

`stop()` waits for running handlers and closes connections. Call it from a
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

Routes match the method and path exactly, in registration order. Query strings
are separate from the path; case and trailing slashes are significant. Named
parameters, wildcard routes, and reusable routers are planned. Register HEAD explicitly
at this stage. `setDefault()` installs a fallback; an empty handler restores 404.

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

Middleware and routes execute in registration order. `next()` synchronously runs
the remaining chain, then returns so middleware can perform cleanup or logging.
`use("/api", ...)` matches `/api` and `/api/...`, but not `/apiculture`. A trailing
slash on the registered prefix is ignored. Prefix matching is case-sensitive,
does not decode URLs, and does not strip the prefix from `req.path`.

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
deferred work. Handlers are synchronous in this stage.

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
normal routing. Each error handler runs at most once per request, and exceptions
from an error handler advance to the remaining handlers. Registration order
relative to ordinary routes does not affect this separate error chain.

Handled errors can keep the connection alive. Unhandled errors receive a generic
500 and close the connection. A normal 404 does not enter the error chain; invalid
HTTP parsing still receives 400 before application dispatch. Because responses
are buffered, exceptions thrown after `send()` also discard that buffered output
and enter error handling; only the final response is written to the socket.

## Documentation

TODO

<!-- For full documentation, please visit [Lightning++ Documentation](https://c-carrasco.github.io/lightningxx/). -->

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
Handlers run synchronously on I/O workers and can execute concurrently for
different connections; synchronize any application state they share. Destroy
the server from its owning thread after running handlers can return. Shutdown
closes idle connections and pending writes.

Run `./build.sh test asan=on ubsan=on` for the regression suite with memory and
undefined-behavior checks. The socket tests use dynamically allocated local
ports and bounded waits and cover fragmented and pipelined requests, binary
and chunked bodies, ownership, handler exceptions, concurrent configuration,
and shutdown.

## Installation

TODO

## Contributing

We welcome contributions from the community.

## License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for details.
