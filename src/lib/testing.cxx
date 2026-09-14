// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>

#include <lightning/testing.h>

#include "http_field_validation.h"


namespace lightning::testing {

// ----------------------------------------------------------------------------
// Testing client request injection and handling
// ----------------------------------------------------------------------------
Response Client::inject (std::string_view wire) const {
  const Logger logger { LogLevel::kFatal };
  HttpRequest req { std::cref (logger) };
  if (!req.parse (wire, _limits))
    throw std::invalid_argument ("Expected one complete HTTP request within the configured limits");

  // Middleware can change req.method; framing follows the original request.
  const auto method = req.method;
  HttpResponse res;
  asio::io_context io;
  std::exception_ptr error;

  asio::co_spawn (io, _dispatch (req, res), [&] (std::exception_ptr failure) { error = failure; });
  io.run();
  if (error)
    std::rethrow_exception (error);

  const auto serialized = res.data (method == HttpMethod::kHead);
  Response result { res.status(), {}, {} };
  auto start = serialized.find ("\r\n") + 2;
  for (;;) {
    const auto end = serialized.find ("\r\n", start);

    if (end == start) {
      result.body = serialized.substr (end + 2);
      return result;
    }

    const auto colon = serialized.find (':', start);
    result.headers.set (std::string_view { serialized }.substr (start, colon - start),
      std::string_view { serialized }.substr (colon + 2, end - colon - 2));
    start = end + 2;
  }
}

// ----------------------------------------------------------------------------
// Testing client request construction and sending
// ----------------------------------------------------------------------------
Response Client::request (
  HttpMethod method,
  std::string_view target,
  std::string_view body,
  const HttpHeader &headers
) const {
  constexpr std::string_view methods[] { "GET", "HEAD", "POST", "PUT", "DELETE",
    "CONNECT", "OPTIONS", "TRACE", "PATCH" };

  const auto index = static_cast<int> (method);
  if (index < 0 || index >= kNumHttpMethods)
    throw std::invalid_argument ("Unknown HTTP method");

  if (target.empty())
    throw std::invalid_argument ("Empty request target");

  for (const unsigned char c : target)
    if (c <= 32 || c == 127) throw std::invalid_argument ("Invalid request target");

  if (headers.contains ("content-length") || headers.contains ("transfer-encoding"))
    throw std::invalid_argument ("Use inject() to specify request framing");

  std::string wire { methods[index] };
  wire += " ";
  wire += target;
  wire += " HTTP/1.1\r\n";
  for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
    detail::validateField (it->first, it->second);
    wire += it->first + ": " + it->second + "\r\n";
  }
  if (!headers.contains ("host"))
    wire += "host: localhost\r\n";
  wire += "content-length: " + std::to_string (body.size()) + "\r\n\r\n";
  wire.append (body);

  return inject (wire);
}

}
