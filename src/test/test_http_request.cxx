// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <cstring>
#include <gtest/gtest.h>
#include <lightning/http_connection.h>
#include "../lib/http_request_parser.h"

namespace {
lightning::Logger logger { lightning::LogLevel::kFatal };
std::string bodyText (const lightning::HttpRequest &request) {
  return { request.body.begin(), request.body.end() };
}
}

TEST (HttpRequest, owns_binary_body_and_headers) {
  lightning::HttpRequest request { std::cref (logger) };
  const std::string body { "a\0bc\xff", 5 };
  {
    std::string input = "POST /body HTTP/1.1\r\nHost: localhost\r\nX-Test: owned\r\nContent-Length: 5\r\n\r\n" + body;
    ASSERT_TRUE (request.parse (input));
    input.assign (input.size(), 'x');
  }
  EXPECT_EQ (bodyText (request), body);
  EXPECT_EQ (request.headers.get ("x-test"), "owned");
}

TEST (HttpRequest, appends_chunked_body) {
  lightning::HttpRequest request { std::cref (logger) };
  ASSERT_TRUE (request.parse (
    "POST /body HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
    "3\r\nabc\r\n2\r\nde\r\n0\r\nX-Trailer: done\r\n\r\n"));
  EXPECT_EQ (bodyText (request), "abcde");
  EXPECT_EQ (request.trailers.get ("x-trailer"), "done");
}

TEST (HttpRequest, rejects_incomplete_invalid_and_multiple_messages) {
  for (const auto input : {
      "", "GET /", "GET / HTTP/1.1\r\nHost: localhost\r\n",
      "POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nab",
      "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n",
      "GET / HTTP/1.1\r\nBad Header: invalid\r\n\r\n",
      "PROPFIND / HTTP/1.1\r\nHost: localhost\r\n\r\n",
      "INVALID / HTTP/1.1\r\n\r\n", "HTTP/1.1 200 OK\r\n\r\n",
      "GET / HTTP/1.1\r\n\r\nGET /other HTTP/1.1\r\n\r\n" }) {
    SCOPED_TRACE (input);
    lightning::HttpRequest request { std::cref (logger) };
    EXPECT_FALSE (request.parse (input));
  }
}

TEST (HttpRequest, initializes_and_resets_fields) {
  lightning::HttpRequest request { std::cref (logger) };
  EXPECT_EQ (request.method, lightning::HttpMethod::kUnknown);
  EXPECT_EQ (request.version.major, 0);
  EXPECT_EQ (request.version.minor, 0);
  EXPECT_EQ (request.statusCode, 0);
  ASSERT_TRUE (request.parse ("POST /old?q=1 HTTP/1.1\r\nHost: old\r\nContent-Length: 1\r\n\r\nx"));
  ASSERT_TRUE (request.parse ("GET /new HTTP/1.0\r\n\r\n"));
  EXPECT_EQ (request.path, "/new");
  EXPECT_TRUE (request.query.empty());
  EXPECT_TRUE (request.host.empty());
  EXPECT_TRUE (request.body.empty());
  EXPECT_EQ (request.headers.size(), 0);
}

TEST (HttpRequest, ipv6_hostname_excludes_only_port) {
  for (const auto host : { "[::1]", "[::1]:8080", "[2001:db8::1]:80", "example.com:80" }) {
    lightning::HttpRequest request { std::cref (logger) };
    ASSERT_TRUE (request.parse (std::string ("GET / HTTP/1.1\r\nHost: ") + host + "\r\n\r\n"));
    const std::string value = host;
    const auto end = value.front() == '[' ? value.find (']') + 1 : value.find (':');
    EXPECT_EQ (request.host, value.substr (0, end));
  }
}

TEST (HttpRequest, incremental_parser_handles_every_split_position) {
  const std::string input =
    "POST /long/path?key=value HTTP/1.1\r\nHost: [::1]:8080\r\n"
    "X-Long-Header: fragmented value\r\nX-Empty:\r\nTransfer-Encoding: chunked\r\n\r\n"
    "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
  using Parser = lightning::detail::HttpRequestParser;
  for (size_t split = 0; split < input.size(); ++split) {
    SCOPED_TRACE (split);
    lightning::HttpRequest request { std::cref (logger) };
    Parser parser { request };
    const auto first = parser.consume (std::string_view (input).substr (0, split));
    ASSERT_EQ (first.status, Parser::Status::kIncomplete);
    EXPECT_EQ (first.consumed, split);
    const auto second = parser.consume (std::string_view (input).substr (split));
    ASSERT_EQ (second.status, Parser::Status::kComplete);
    EXPECT_EQ (second.consumed, input.size() - split);
    EXPECT_TRUE (second.keepAlive);
    EXPECT_EQ (request.method, lightning::HttpMethod::kPost);
    EXPECT_EQ (request.path, "/long/path");
    EXPECT_EQ (request.query, "key=value");
    EXPECT_EQ (request.host, "[::1]");
    EXPECT_EQ (request.headers.get ("x-long-header"), "fragmented value");
    EXPECT_EQ (request.headers.get ("x-empty"), "");
    EXPECT_EQ (bodyText (request), "abcde");
  }
}

TEST (InputBuffer, respects_lengths_offsets_and_embedded_nulls) {
  lightning::InputBuffer buffer;
  auto storage = buffer.makeAsioBuffer();
  std::memset (storage.data(), 'x', storage.size());
  buffer.obtainedBytes (storage.size());
  EXPECT_EQ (buffer.str().size(), storage.size());
  std::memcpy (storage.data(), "a\0bc", 4);
  buffer.obtainedBytes (4);
  EXPECT_EQ (buffer.str(), std::string_view ("a\0bc", 4));
  buffer.consumedBytes (2);
  EXPECT_EQ (buffer.str(), "bc");
  EXPECT_EQ (*buffer.bytes(), 'b');
  buffer.consumedBytes (2);
  EXPECT_TRUE (buffer.str().empty());
  EXPECT_THROW (buffer.consumedBytes (1), std::out_of_range);
  EXPECT_THROW (buffer.obtainedBytes (storage.size() + 1), std::out_of_range);
}

TEST (HttpResponse, defaults_to_200_and_owns_temporary_header) {
  lightning::HttpResponse response;
  response.headers().set ("x-owned", std::string (256, 'x'));
  const auto wire = response.send ("hello").data();
  EXPECT_EQ (wire.substr (0, 12), "HTTP/1.1 200");
  EXPECT_NE (wire.find ("x-owned: " + std::string (256, 'x') + "\r\n"), std::string::npos);
  EXPECT_NE (wire.find ("content-length: 5\r\n"), std::string::npos);
  EXPECT_EQ (wire.substr (wire.size() - 5), "hello");
}
