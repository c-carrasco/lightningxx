// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <stdexcept>
#include <gtest/gtest.h>
#include <lightning/http_server.h>
#include "http_test_client.h"

namespace {
using namespace std::chrono_literals;
using lightning::HttpMethod;
using lightning::HttpServer;
using lightning::LogLevel;

using lightning::test::Client;

std::string get (std::string_view path) {
  return "GET " + std::string (path) + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}
void addOk (HttpServer &server) {
  server.addRoute (HttpMethod::kGet, "/ok", [] (const auto &, auto &response) { response.send ("ok"); });
}

class HttpMethods: public ::testing::TestWithParam<std::pair<HttpMethod, const char *>> {};

TEST_P (HttpMethods, parses_request_and_returns_response) {
  HttpServer server { 0, LogLevel::kFatal };
  const auto [method, name] = GetParam();
  const std::string body = method == HttpMethod::kGet || method == HttpMethod::kConnect ? "" : "aa=bb";
  std::promise<lightning::HttpRequest> received;
  auto future = received.get_future();
  server.addRoute (method, "/hello", [&received] (const auto &request, auto &response) {
    received.set_value (request);
    response.send ("Hello World!");
  });
  Client client { server.port() };
  client.send (std::string (name) + " /hello?param=123 HTTP/1.1\r\nHost: localhost:" +
    std::to_string (server.port()) + "\r\nHeaderName: header value\r\nContent-Length: " +
    std::to_string (body.size()) + "\r\n\r\n" + body);
  const auto response = client.read (method == HttpMethod::kHead);
  EXPECT_EQ (response.status, 200);
  EXPECT_EQ (response.body, method == HttpMethod::kHead ? "" : "Hello World!");
  EXPECT_EQ (response.headers.at ("content-length"), "12");
  ASSERT_EQ (future.wait_for (1s), std::future_status::ready);
  const auto request = future.get();
  EXPECT_EQ (request.method, method);
  EXPECT_EQ (request.path, "/hello");
  EXPECT_EQ (request.query, "param=123");
  EXPECT_EQ (request.url, "/hello?param=123");
  EXPECT_EQ (request.version.major, 1);
  EXPECT_EQ (request.version.minor, 1);
  EXPECT_EQ (request.host, "localhost");
  EXPECT_EQ (request.ip, "127.0.0.1");
  EXPECT_EQ (request.protocol, lightning::ProtocolType::kHttp);
  EXPECT_EQ (request.headers.get ("HEADERNAME"), "header value");
  EXPECT_EQ (std::string (request.body.begin(), request.body.end()), body);
}

INSTANTIATE_TEST_SUITE_P (AllMethods, HttpMethods, ::testing::Values (
  std::make_pair (HttpMethod::kGet, "GET"), std::make_pair (HttpMethod::kHead, "HEAD"),
  std::make_pair (HttpMethod::kPost, "POST"), std::make_pair (HttpMethod::kPut, "PUT"),
  std::make_pair (HttpMethod::kDelete, "DELETE"), std::make_pair (HttpMethod::kConnect, "CONNECT"),
  std::make_pair (HttpMethod::kOptions, "OPTIONS"), std::make_pair (HttpMethod::kTrace, "TRACE"),
  std::make_pair (HttpMethod::kPatch, "PATCH")));

TEST (HttpServer, fragmented_headers_wait_for_completion) {
  HttpServer server { 0, LogLevel::kFatal };
  addOk (server);
  Client client { server.port() };
  client.send ("G");
  EXPECT_FALSE (client.hasDataWithin (20ms));
  client.send ("ET /ok HTTP/1.1\r\nHost: local");
  EXPECT_FALSE (client.hasDataWithin (20ms));
  client.send ("host\r\n");
  EXPECT_FALSE (client.hasDataWithin (20ms));
  client.send ("\r\n");
  EXPECT_EQ (client.read().body, "ok");
}

TEST (HttpServer, waits_for_entire_binary_body_across_reads) {
  HttpServer server { 0, LogLevel::kFatal };
  std::atomic<int> calls { 0 };
  server.addRoute (HttpMethod::kPost, "/body", [&calls] (const auto &request, auto &response) {
    ++calls;
    response.send (std::string (request.body.begin(), request.body.end()));
  });
  std::string body (10000, 'x');
  body[0] = '\0';
  body[5000] = '\0';
  body.back() = 'z';
  Client client { server.port() };
  client.send ("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10000\r\n\r\n");
  EXPECT_FALSE (client.hasDataWithin (20ms));
  EXPECT_EQ (calls, 0);
  client.send (std::string_view (body).substr (0, 6000));
  EXPECT_FALSE (client.hasDataWithin (20ms));
  EXPECT_EQ (calls, 0);
  client.send (std::string_view (body).substr (6000));
  EXPECT_EQ (client.read().body, body);
  EXPECT_EQ (calls, 1);
}

TEST (HttpServer, chunked_body_and_following_request_remain_separate) {
  HttpServer server { 0, LogLevel::kFatal };
  addOk (server);
  server.addRoute (HttpMethod::kPost, "/body", [] (const auto &request, auto &response) {
    response.send (std::string (request.body.begin(), request.body.end()));
  });
  Client client { server.port() };
  client.send ("POST /body HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n");
  EXPECT_FALSE (client.hasDataWithin (20ms));
  client.send ("2\r\nde\r\n0\r\n\r\n" + get ("/ok"));
  EXPECT_EQ (client.read().body, "abcde");
  EXPECT_EQ (client.read().body, "ok");
}

TEST (HttpServer, pipelines_responses_in_order_and_handles_shorter_next_read) {
  HttpServer server { 0, LogLevel::kFatal };
  addOk (server);
  Client client { server.port() };
  client.send (get ("/ok") + get ("/a-much-longer-missing-path") + get ("/ok"));
  EXPECT_EQ (client.read().status, 200);
  EXPECT_EQ (client.read().status, 404);
  EXPECT_EQ (client.read().body, "ok");
  client.send (get ("/ok"));
  EXPECT_EQ (client.read().body, "ok");
  EXPECT_FALSE (client.hasDataWithin (20ms));
}

TEST (HttpServer, rejects_invalid_requests_without_calling_handlers) {
  HttpServer server { 0, LogLevel::kFatal };
  std::atomic<int> calls { 0 };
  server.addRoute (HttpMethod::kGet, "/ok", [&calls] (const auto &, auto &response) { ++calls; response.send ("bad"); });
  server.setDefault ([&calls] (const auto &, auto &response) { ++calls; response.send ("bad"); });
  for (const auto &input : {
      std::string ("GET /ok HTTP/1.1\r\nBad Header: invalid\r\n\r\n"),
      std::string ("INVALID /ok HTTP/1.1\r\n\r\n"),
      std::string ("PROPFIND /ok HTTP/1.1\r\n\r\n"),
      std::string ("HTTP/1.1 200 OK\r\n\r\n"),
      std::string ("GET /ok HTTP/1.1\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n") }) {
    SCOPED_TRACE (input);
    Client client { server.port() };
    client.send (input + get ("/ok"));
    EXPECT_EQ (client.read().status, 400);
    EXPECT_TRUE (client.waitForClose());
  }
  EXPECT_EQ (calls, 0);
}

TEST (HttpServer, truncated_request_at_eof_is_rejected) {
  HttpServer server { 0, LogLevel::kFatal };
  Client client { server.port() };
  client.send ("POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nab");
  client.finishSending();
  EXPECT_EQ (client.read().status, 400);
  EXPECT_TRUE (client.waitForClose());
}

TEST (HttpServer, response_buffer_survives_large_asynchronous_write) {
  HttpServer server { 0, LogLevel::kFatal };
  const std::string body (4 * 1024 * 1024, 'x');
  server.addRoute (HttpMethod::kGet, "/big", [&body] (const auto &, auto &response) { response.send (body); });
  Client client { server.port() };
  client.send (get ("/big"));
  ASSERT_TRUE (client.hasDataWithin (1s));
  std::this_thread::sleep_for (20ms);
  const auto response = client.read();
  EXPECT_EQ (response.status, 200);
  EXPECT_EQ (response.body, body);
}

TEST (HttpServer, owns_dynamic_routes_and_honors_view_length) {
  HttpServer server { 0, LogLevel::kFatal };
  server.addRoute (HttpMethod::kGet, "/" + std::string (256, 'a'), [] (const auto &, auto &response) { response.send ("owned"); });
  server.addRoute (HttpMethod::kGet, std::string_view ("/slice_extra", 6), [] (const auto &, auto &response) { response.send ("slice"); });
  Client client { server.port() };
  client.send (get ("/" + std::string (256, 'a')) + get ("/slice") + get ("/slice_extra"));
  EXPECT_EQ (client.read().body, "owned");
  EXPECT_EQ (client.read().body, "slice");
  EXPECT_EQ (client.read().status, 404);
}

TEST (HttpServer, contains_route_and_default_handler_exceptions) {
  HttpServer server { 0, LogLevel::kFatal };
  addOk (server);
  server.addRoute (HttpMethod::kGet, "/throw", [] (const auto &, auto &response) {
    response.headers().set ("x-partial", "discard");
    response.send ("partial");
    throw std::runtime_error ("private exception detail");
  });
  server.setDefault ([] (const auto &, auto &) { throw 42; });
  for (const auto path : { "/throw", "/missing" }) {
    Client client { server.port() };
    client.send (get (path));
    const auto response = client.read();
    EXPECT_EQ (response.status, 500);
    EXPECT_EQ (response.body, "Internal server error");
    EXPECT_EQ (response.headers.count ("x-partial"), 0);
    EXPECT_TRUE (client.waitForClose());
  }
  Client client { server.port() };
  client.send (get ("/ok"));
  EXPECT_EQ (client.read().body, "ok");
}

TEST (HttpServer, shuts_down_with_idle_and_partial_connections) {
  auto server = std::make_unique<HttpServer> (0, 3, LogLevel::kFatal);
  addOk (*server);
  Client idle { server->port() };
  idle.send (get ("/ok"));
  EXPECT_EQ (idle.read().status, 200);
  Client partial { server->port() };
  partial.send ("GET /ok HTTP/1.1\r\n");
  auto stopped = std::async (std::launch::async, [server = std::move (server)] () mutable { server.reset(); });
  EXPECT_EQ (stopped.wait_for (1s), std::future_status::ready);
  idle.close();
  partial.close();
  stopped.get();
}

TEST (HttpServer, shuts_down_with_pending_response_write) {
  auto server = std::make_unique<HttpServer> (0, 3, LogLevel::kFatal);
  server->addRoute (HttpMethod::kGet, "/big", [] (const auto &, auto &response) { response.send (std::string (4 * 1024 * 1024, 'x')); });
  Client client { server->port() };
  client.send (get ("/big"));
  EXPECT_EQ (client.readHeaders().status, 200);
  auto stopped = std::async (std::launch::async, [server = std::move (server)] () mutable { server.reset(); });
  EXPECT_EQ (stopped.wait_for (1s), std::future_status::ready);
  client.close();
  stopped.get();
}

TEST (HttpServer, supports_concurrent_registration_and_default_changes) {
  HttpServer server { 0, 4, LogLevel::kFatal };
  addOk (server);
  std::atomic<int> failures { 0 };
  std::atomic<bool> start { false };
  std::vector<std::thread> clients;
  for (int i = 0; i < 3; ++i) {
    clients.emplace_back ([&] {
      while (!start.load()) std::this_thread::yield();
      try {
        Client client { server.port() };
        for (int j = 0; j < 50; ++j) {
          client.send (get ("/ok") + get ("/missing"));
          if (client.read().body != "ok") ++failures;
          const auto status = client.read().status;
          if (status != 404 && status != 410 && status != 411) ++failures;
        }
      }
      catch (...) { ++failures; }
    });
  }
  start = true;
  for (int i = 0; i < 500; ++i) {
    server.addRoute (HttpMethod::kGet, "/dynamic/" + std::to_string (i), [] (const auto &, auto &response) { response.send ("dynamic"); });
    server.setDefault ([status = 410 + i % 2] (const auto &, auto &response) { response.status (status).send ("fallback"); });
    server.setLogLevel (LogLevel::kFatal);
  }
  for (auto &client : clients) client.join();
  EXPECT_EQ (failures, 0);
}

TEST (HttpServer, invokes_handlers_outside_configuration_lock) {
  HttpServer server { 0, 2, LogLevel::kFatal };
  server.addRoute (HttpMethod::kGet, "/register", [&server] (const auto &, auto &response) {
    addOk (server);
    server.setDefault ([] (const auto &, auto &fallback) { fallback.status (410).send ("gone"); });
    response.send ("registered");
  });
  Client client { server.port() };
  client.send (get ("/register"));
  EXPECT_EQ (client.read().body, "registered");
  client.send (get ("/ok") + get ("/missing"));
  EXPECT_EQ (client.read().body, "ok");
  EXPECT_EQ (client.read().status, 410);
}

TEST (HttpServer, head_response_does_not_corrupt_following_response) {
  HttpServer server { 0, LogLevel::kFatal };
  addOk (server);
  server.addRoute (HttpMethod::kHead, "/ok", [] (const auto &, auto &response) { response.send ("ok"); });
  Client client { server.port() };
  client.send ("HEAD /ok HTTP/1.1\r\n\r\n" + get ("/ok"));
  EXPECT_EQ (client.read (true).headers.at ("content-length"), "2");
  EXPECT_EQ (client.read().body, "ok");
}

TEST (HttpServer, closes_connection_when_requested) {
  HttpServer server { 0, LogLevel::kFatal };
  addOk (server);
  Client client { server.port() };
  client.send ("GET /ok HTTP/1.1\r\nConnection: close\r\n\r\n" + get ("/ok"));
  const auto response = client.read();
  EXPECT_EQ (response.body, "ok");
  EXPECT_EQ (response.headers.at ("connection"), "close");
  EXPECT_TRUE (client.waitForClose());
}

TEST (HttpServer, rejects_zero_workers_and_invalid_registration) {
  EXPECT_THROW ((HttpServer { 0, 0, LogLevel::kFatal }), std::invalid_argument);
  HttpServer server { 0, LogLevel::kFatal };
  EXPECT_THROW (server.addRoute (HttpMethod::kUnknown, "/", [] (const auto &, auto &) {}), std::invalid_argument);
  EXPECT_THROW (server.addRoute (HttpMethod::kGet, "/", {}), std::invalid_argument);
}

TEST (HttpServer, requires_dispatcher_and_serves_its_preconfigured_routes) {
  lightning::ServerOptions options;
  options.port = 0;
  options.logLevel = LogLevel::kFatal;
  EXPECT_THROW ((HttpServer { options, nullptr }), std::invalid_argument);
  auto dispatcher = std::make_shared<lightning::Dispatcher>();
  dispatcher->addRoute (HttpMethod::kGet, "/ready", [] (const auto &, auto &res) { res.send ("ready"); });
  HttpServer server { options, dispatcher };
  dispatcher.reset(); // The server owns the dispatcher for its entire lifetime.
  Client client { server.port() };
  client.send (get ("/ready"));
  EXPECT_EQ (client.read().body, "ready");
}

}
