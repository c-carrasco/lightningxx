// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <gtest/gtest.h>

#include <cpp20_http_client.hpp>

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
TEST (HttpHeader, test_get_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  server.addRoute (lightning::HttpMethod::kGet, "/hello", [] ([[maybe_unused]] const auto &request, auto &response) {
    response.status(200).send ("Hello World!");
  });

  const auto response { http_client::get("http://localhost:8080/hello").send() };
  ASSERT_EQ (response.get_status_code(), http_client::StatusCode::Ok);
  ASSERT_EQ (response.get_http_version(), "HTTP/1.1");
  ASSERT_EQ (response.get_body_string(), "Hello World!");
}