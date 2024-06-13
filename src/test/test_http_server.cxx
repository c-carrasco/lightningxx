// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <gtest/gtest.h>

#include <httplib.h>

#include <lightning/http_server.h>

// ----------------------------------------------------------------------------
// getLogLevel
// ----------------------------------------------------------------------------
inline lightning::LogLevel getLogLevel (const lightning::LogLevel defLevel = lightning::LogLevel::kWarn) {
  if (const auto s = std::getenv ("LIGHTNINGXX_TEST_LOG_LEVEL")) {
    if ((std::strcmp (s, "verbose") == 0) || (std::strcmp (s, "v") == 0)) return lightning::LogLevel::kVerbose;
    if ((std::strcmp (s, "debug") == 0) || (std::strcmp (s, "d") == 0)) return lightning::LogLevel::kDebug;
    if ((std::strcmp (s, "info") == 0) || (std::strcmp (s, "i") == 0)) return lightning::LogLevel::kInfo;
    if ((std::strcmp (s, "warn") == 0) || (std::strcmp (s, "w") == 0)) return lightning::LogLevel::kWarn;
    if ((std::strcmp (s, "error") == 0) || (std::strcmp (s, "e") == 0)) return lightning::LogLevel::kError;
    if ((std::strcmp (s, "fatal") == 0) || (std::strcmp (s, "f") == 0)) return lightning::LogLevel::kFatal;
  }

  return defLevel;
}


// ----------------------------------------------------------------------------
// test_get_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_get_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  server.addRoute (lightning::HttpMethod::kGet, "/hello", [] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kGet);
    ASSERT_EQ (request.path, "/hello");
    ASSERT_EQ (request.query, "param=123&test=abc");
    ASSERT_EQ (request.url, "/hello?param=123&test=abc");
    ASSERT_EQ (request.version.major, 1);
    ASSERT_EQ (request.version.minor, 1);
    ASSERT_EQ (request.host, "localhost");
    ASSERT_EQ (request.ip, "127.0.0.1");
    ASSERT_EQ (request.protocol, lightning::ProtocolType::kHttp);
    ASSERT_TRUE (request.headers.contains("host"));
    ASSERT_EQ (request.headers.get("host"), "localhost:8080");
    ASSERT_TRUE (request.headers.contains("headername"));
    ASSERT_EQ (request.headers.get("HeaderName"), "header value");
    ASSERT_EQ (request.body.size(), 0);
    response.status(200).send ("Hello World!");
  });

  httplib::Client client { "localhost", 8080 };
  const auto response { client.Get ("/hello?param=123&test=abc", {
    { "HeaderName", "header value" }
  }) };

  ASSERT_EQ (response->status, 200);
  ASSERT_EQ (response->version, "HTTP/1.1");
  ASSERT_EQ (response->body, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_post_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_post_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kPost, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kPost);
    ASSERT_EQ (request.path, "/v1/hello");
    ASSERT_EQ (request.query, "p=1");
    ASSERT_EQ (request.url, "/v1/hello?p=1");
    ASSERT_EQ (request.version.major, 1);
    ASSERT_EQ (request.version.minor, 1);
    ASSERT_EQ (request.host, "localhost");
    ASSERT_EQ (request.ip, "127.0.0.1");
    ASSERT_EQ (request.protocol, lightning::ProtocolType::kHttp);
    ASSERT_TRUE (request.headers.contains("host"));
    ASSERT_TRUE (request.headers.contains("headername"));
    ASSERT_EQ (request.headers.get("content-length"), std::to_string(body.size()));
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  httplib::Client client { "localhost", 8080 };
  const auto response { client.Post ("/v1/hello?p=1", {
    { "HeaderName", "header value" },
    { "Content-Type", "text/plain" }
  }, body, "text/plain") };

  ASSERT_EQ (response->status, 200);
  ASSERT_EQ (response->version, "HTTP/1.1");
  ASSERT_EQ (response->body, "Hello World!");
}
