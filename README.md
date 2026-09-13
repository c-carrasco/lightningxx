Lightning++
===========

[![ci](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml/badge.svg)](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml)

## Introduction

Lightning++ is a lightweight, expressive, and flexible web framework for C++. Designed to facilitate the rapid development of web applications and APIs, Lightning++ offers a streamlined approach to handling HTTP requests and responses, middleware integration, and routing with a focus on high performance and minimal overhead.

## Features

- **Expressive Routing**: Define routes using easy-to-understand syntax to respond to HTTP requests.
- **Middleware Support**: Enhance functionality with middleware at the application or route level for tasks like logging, parsing, and session management.
- **Fast and Lightweight**: Optimized for speed and a low memory footprint to deliver high performance.
- **Asynchronous Support**: Handle requests asynchronously, leveraging modern C++ features to manage non-blocking I/O operations.
- **Header-Only**: Easy to integrate into any C++ project as a header-only library, simplifying dependency management.

## Quick Start

TODO

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

Examples:

```bash
# Compile code in release mode
./build.sh clean release

# Build in debug mode with Clang 17 and AddressSanitizer, then run unit tests
./build.sh docker=clang17 debug test asan=on

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
