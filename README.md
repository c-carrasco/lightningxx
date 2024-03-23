Lightning++
===========

[![ci](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml/badge.svg)](https://github.com/c-carrasco/lightningxx/actions/workflows/main.yml)

# Introduction

Lightning++ is a lightweight, expressive, and flexible web framework for C++. Designed to facilitate the rapid development of web applications and APIs, Lightning++ offers a streamlined approach to handling HTTP requests and responses, middleware integration, and routing with a focus on high performance and minimal overhead.

# Features

- **Expressive Routing**: Define routes using easy-to-understand syntax to respond to HTTP requests.
- **Middleware Support**: Enhance functionality with middleware at the application or route level for tasks like logging, parsing, and session management.
- **Fast and Lightweight**: Optimized for speed and low memory footprint to deliver high performance.
- **Asynchronous Support**: Handle requests asynchronously, leveraging modern C++ features to manage non-blocking I/O operations.
- **Header Only**: Easy to integrate into any C++ project as a header-only library, simplifying dependency management.

# Quick Start

TODO

# Documentation

TODO

<!-- For full documentation, please visit [Lightning++ Documentation](https://c-carrasco.github.io/lightningxx/). -->

# Prerequisites to build this project

Before diving into `Lightning++`, make sure you have the following tools and dependencies set up:

- Conan 1.x
- CMake 3.20 or higer
- GCC +13, Clang +16 or AppleClang +15
- GNU Make or ninja
- Docker (optional)
- Doxygen (for generating documentation)

# Build the project

## Achieve Reproducible Builds with Docker

For a streamlined development environment, Docker is your friend. It's highly recommended to utilize Docker for building your code.

## The Building Script

Located in the root folder of the project, the build.sh file acts as the catalyst for your building endeavors.

**Usage Guide**
---------------
* **_release_**
Construct the code in release mode.
* **_debug_**
Construct the code in debug mode (selected if no mode is specified).
* **_clean_**
Wipe out the _build_ directory along with its contents.
* **_verbose_**
Force GNU make to show the commands.
* **_ninja_**
Opt for `ninja` over GNU Make for code compilation.
* **_tests_**
Execute tests post compilation.
* **_asan=on_**
Enable the [Address Sanitizer]
* **_ubsan=on_**
Enable the [Undefined Behavior Sanitizer]
* **_docker[=compiler]_**
Use docker for local development.
  Available options:
    - `gcc13`: Use GCC 13 as default compiler (selected if no compiler is specified).
    - `clang16`: Use Clang 16 as default compiler.

Examples:

```
# Compile code in release mode
./build.sh clean release

# Build code in debug mode using Docker image with Clang 16, Address Sanitizer enabled, and run unit tests
./build.sh docker=clang16 debug test asan=on

# Start docker dev environment with gcc13
./build.sh docker=gcc13
```

# How to use it

TODO

# Installation

TODO

# Contributing

We welcome contributions from the community.

# License
This project is licensed under the MIT License - see the [LICENSE](./LICENSE) file for details.