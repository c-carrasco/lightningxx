// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_TRANSPORT_OPTIONS_H
#define LIGHTNING_TRANSPORT_OPTIONS_H
#include <chrono>
#include <cstddef>


namespace lightning {

struct RequestLimits {
  size_t headerBytes { 16 * 1024 }; // Initial request line and headers, including delimiters.
  size_t targetBytes { 8 * 1024 };
  size_t headerCount { 100 }; // Includes trailers.
  size_t bodyBytes { 1024 * 1024 }; // Decoded chunk payload, before body middleware.
};

struct TransportOptions {
  RequestLimits limits;
  // Absolute phase deadlines, not reset by fragments. Zero disables a timeout.
  std::chrono::milliseconds headerTimeout { 10000 };
  std::chrono::milliseconds bodyTimeout { 30000 };
  std::chrono::milliseconds writeTimeout { 30000 };
  std::chrono::milliseconds keepAliveTimeout { 5000 };
  std::chrono::milliseconds handlerTimeout { 30000 }; // Includes coroutine suspension.
};

}

#endif
