Lightning++
===========

[![ci](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml/badge.svg)](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml)

## Introduction

Lightning++ is a lightweight, expressive, and flexible web framework for C++. Designed to facilitate the rapid development of web applications and APIs, Lightning++ offers a streamlined approach to handling HTTP requests and responses, middleware integration, and routing with a focus on high performance and minimal overhead.

## Features

- **Expressive Routing**: Register exact method/path routes using `app.get()`, `app.post()`, and other method helpers.
- **Middleware Support (planned)**: Application and router middleware for logging, parsing, and session management.
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
parameters, wildcard routes, and middleware are planned. Register HEAD explicitly
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
socket. It shares the `Dispatcher` used by `HttpServer`. Direct dispatch propagates
handler exceptions to the caller; socket requests retain the 500 response and
connection-close behavior described below. It does not perform HTTP parsing or
wire-level HEAD body suppression.

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
exceptions from route or default handlers receive 500 and close that connection.

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
