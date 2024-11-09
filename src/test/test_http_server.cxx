// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <lightning/http_server.h>

// ----------------------------------------------------------------------------
// getLogLevel
// ----------------------------------------------------------------------------
static lightning::LogLevel getLogLevel (const lightning::LogLevel defLevel = lightning::LogLevel::kWarn) {
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
// createTempFiles
// ----------------------------------------------------------------------------
static std::pair<std::filesystem::path, std::filesystem::path> createTempFiles () {//const std::string &name) {
  const std::string name { ::testing::UnitTest::GetInstance()->current_test_info()->name() };
  auto statusFileName {std::filesystem::temp_directory_path() / name };
  auto bodyFileName { std::filesystem::temp_directory_path() / name };

  return { statusFileName.replace_extension (".status"), bodyFileName.replace_extension (".body") };
}

// ----------------------------------------------------------------------------
// readResponse
// ----------------------------------------------------------------------------
static std::pair<int32_t, std::string> readResponse (
  const std::filesystem::path &status,
  const std::filesystem::path &body
) {
  std::ifstream statusFile { status };
  std::ifstream bodyFile { body };

  std::string statusLine;
  std::getline (statusFile, statusLine);

  std::string bodyContent { std::istreambuf_iterator<char> { bodyFile }, std::istreambuf_iterator<char> {} };

  return { std::stoi (statusLine), bodyContent };
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

  const auto [ statusFileName, bodyFileName ] = createTempFiles ();

  const auto exit = std::system (fmt::format(
    "curl -s -X GET 'http://localhost:8080/hello?param=123&test=abc' -H 'HeaderName: header value' -w '%{{http_code}}' -o {} > {}",
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
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
    // ASSERT_EQ (request.headers.get("content-length"), std::to_string(body.size()));
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X POST 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_put_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_put_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kPut, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kPut);
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
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X PUT 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_head_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_head_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kHead, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kHead);
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
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X HEAD 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_delete_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_delete_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kDelete, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kDelete);
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
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X DELETE 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_connect_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_connect_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  server.addRoute (lightning::HttpMethod::kConnect, "/v1/hello", [] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kConnect);
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
    ASSERT_EQ (request.body.size(), 0);
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X CONNECT 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -w '%{{http_code}}' -o {} > {}",
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_options_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_options_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kOptions, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kOptions);
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
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X OPTIONS 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_trace_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_trace_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kTrace, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kTrace);
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
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X TRACE 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}

// ----------------------------------------------------------------------------
// test_patch_simple_text
// ----------------------------------------------------------------------------
TEST (HttpServer, test_patch_simple_text) {
  lightning::HttpServer server { 8080, getLogLevel() };

  const std::string body { "aa=bb" };

  server.addRoute (lightning::HttpMethod::kPatch, "/v1/hello", [ &body ] (const auto &request, auto &response) {
    ASSERT_EQ (request.method, lightning::HttpMethod::kPatch);
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
    ASSERT_EQ (request.body.size(), body.size());
    response.status(200).send ("Hello World!");
  });

  const auto [ statusFileName, bodyFileName ] = createTempFiles();

  const auto exit = std::system (fmt::format(
    "curl -s -X PATCH 'http://localhost:8080/v1/hello?p=1' -H 'HeaderName: header value' -H 'Content-Type: text/plain' -d '{}' -w '%{{http_code}}' -o {} > {}",
    body,
    bodyFileName.string(),
    statusFileName.string()
  ).c_str());
  ASSERT_EQ (exit, 0);

  const auto [ resStatus, resBody ] = readResponse (statusFileName, bodyFileName);
  ASSERT_EQ (resStatus, 200);
  ASSERT_EQ (resBody, "Hello World!");
}
