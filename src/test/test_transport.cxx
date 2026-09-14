// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <gtest/gtest.h>

#include <lightning/app.h>

#include "http_test_client.h"
#include "../lib/http_request_parser.h"

namespace {
using namespace lightning;
using namespace std::chrono_literals;
Logger logger { LogLevel::kFatal };
using Parser = detail::HttpRequestParser;

ServerOptions options() {
  ServerOptions result;
  result.port = 0; result.workers = 3; result.logLevel = LogLevel::kFatal;
  return result;
}
std::string get (std::string_view path = "/") {
  return "GET " + std::string (path) + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}
std::string body (const HttpResponse &res) {
  const auto wire = res.data(); return wire.substr (wire.find ("\r\n\r\n") + 4);
}
HttpResponse run (const App &app, HttpMethod method, std::string_view path = "/") {
  HttpRequest req { std::cref (logger) };
  req.method = method; req.path = path;
  HttpResponse res; app.dispatch (req, res); return res;
}

TEST (ReceiveLimits, declared_body_is_rejected_at_headers_before_any_body_allocation) {
  HttpRequest req { std::cref (logger) };
  Parser parser { req, { .bodyBytes = 4 } };
  const auto result = parser.consume ("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\n");
  EXPECT_EQ (result.status, Parser::Status::kInvalid);
  EXPECT_EQ (result.errorStatus, 413);
  EXPECT_TRUE (req.body.empty());
}

TEST (ReceiveLimits, body_boundary_and_pipeline_accounting_survive_every_fragment_boundary) {
  const std::string wire = "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nabcd";
  for (size_t split = 1; split < wire.size(); ++split) {
    HttpRequest req { std::cref (logger) };
    Parser parser { req, { .bodyBytes = 4 } };
    EXPECT_EQ (parser.consume (std::string_view { wire }.substr (0, split)).status, Parser::Status::kIncomplete);
    const auto result = parser.consume (wire.substr (split) + get());
    EXPECT_EQ (result.status, Parser::Status::kComplete) << split;
    EXPECT_EQ (result.consumed, wire.size() - split);
    EXPECT_EQ (req.body.size(), 4);
  }
}

TEST (ReceiveLimits, chunk_size_and_cumulative_chunk_sizes_are_checked_before_allocating) {
  const std::string headers = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
  HttpRequest req { std::cref (logger) };
  Parser parser { req, { .bodyBytes = 4 } };
  EXPECT_EQ (parser.consume (headers + "3\r\nabc\r\n").status, Parser::Status::kIncomplete);
  const auto rejected = parser.consume ("2\r\n");
  EXPECT_EQ (rejected.status, Parser::Status::kInvalid);
  EXPECT_EQ (rejected.errorStatus, 413);
  EXPECT_EQ (req.body.size(), 3);
  HttpRequest huge { std::cref (logger) };
  Parser large { huge, { .bodyBytes = 4 } };
  EXPECT_EQ (large.consume (headers + "ffffffff\r\n").errorStatus, 413);
  EXPECT_TRUE (huge.body.empty());
}

TEST (ReceiveLimits, header_bytes_include_start_line_delimiters_and_whitespace) {
  const auto wire = get();
  HttpRequest exact { std::cref (logger) };
  EXPECT_TRUE (exact.parse (wire, { .headerBytes = wire.size() }));
  HttpRequest req { std::cref (logger) };
  Parser parser { req, { .headerBytes = wire.size() - 1 } };
  EXPECT_EQ (parser.consume (wire).errorStatus, 431);
  HttpRequest spaces { std::cref (logger) };
  Parser padding { spaces, { .headerBytes = 64 } };
  EXPECT_EQ (padding.consume ("GET / HTTP/1.1\r\nX:" + std::string (100, ' ')).errorStatus, 431);
  EXPECT_TRUE (spaces.headers.size() == 0);
}

TEST (ReceiveLimits, header_counter_is_incremental_and_excludes_coalesced_body) {
  const std::string headers = "POST / HTTP/1.1\r\nContent-Length: 100\r\n\r\n";
  for (size_t split = 1; split < headers.size(); ++split) {
    HttpRequest req { std::cref (logger) };
    Parser parser { req, { .headerBytes = headers.size() } };
    EXPECT_EQ (parser.consume (std::string_view { headers }.substr (0, split)).status, Parser::Status::kIncomplete);
    EXPECT_EQ (parser.consume (headers.substr (split) + std::string (100, 'x')).status, Parser::Status::kComplete);
  }
}

TEST (ReceiveLimits, target_and_header_count_limits_are_inclusive) {
  HttpRequest req { std::cref (logger) };
  EXPECT_TRUE (req.parse (get ("/abc"), { .targetBytes = 4, .headerCount = 1 }));
  Parser parser { req, { .targetBytes = 3 } };
  // A parser starts with a fresh request, like the connection does.
  req = HttpRequest { std::cref (logger) };
  const auto target = parser.consume (get ("/abc"));
  EXPECT_EQ (target.errorStatus, 414);
  EXPECT_LE (req.url.size(), 3);
  HttpRequest many { std::cref (logger) };
  Parser count { many, { .headerCount = 1 } };
  EXPECT_EQ (count.consume ("GET / HTTP/1.1\r\nX: 1\r\nX: 2\r\n\r\n").errorStatus, 431);
  EXPECT_EQ (many.headers.size(), 1);
}

TEST (ReceiveLimits, zero_body_limit_allows_empty_requests_only) {
  HttpRequest req { std::cref (logger) };
  EXPECT_TRUE (req.parse (get(), { .bodyBytes = 0 }));
  EXPECT_TRUE (req.parse ("POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n", { .bodyBytes = 0 }));
  EXPECT_FALSE (req.parse ("POST / HTTP/1.1\r\nContent-Length: 1\r\n\r\nx", { .bodyBytes = 0 }));
  EXPECT_FALSE (req.parse (get(), { .headerBytes = 0 }));
  EXPECT_FALSE (req.parse (get(), { .headerCount = 0 }));
}

TEST (RequestFraming, duplicate_fields_are_combined_and_trailers_are_separate) {
  HttpRequest req { std::cref (logger) };
  EXPECT_TRUE (req.parse ("POST / HTTP/1.1\r\nX: first\r\nX: second\r\nCookie: a=1\r\nCookie: b=2\r\n"
    "Transfer-Encoding: chunked\r\n\r\n1\r\nx\r\n0\r\nX: trailer\r\n\r\n"));
  EXPECT_EQ (req.headers.get ("x"), "first, second");
  EXPECT_EQ (req.headers.get ("cookie"), "a=1; b=2");
  EXPECT_EQ (req.trailers.get ("x"), "trailer");
  EXPECT_TRUE (req.parse (get()));
  EXPECT_EQ (req.trailers.size(), 0);
}

TEST (RequestFraming, framing_and_interpretation_fields_cannot_be_replaced_by_trailers) {
  for (const auto name : { "Content-Length", "Transfer-Encoding", "Host", "Connection", "Content-Type",
      "Content-Encoding", "Authorization", "Expect", "Trailer" }) {
    HttpRequest req { std::cref (logger) };
    EXPECT_FALSE (req.parse ("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n" +
      std::string (name) + ": x\r\n\r\n")) << name;
  }
}

TEST (ReceiveLimits, trailer_fields_share_count_and_storage_budgets_with_headers) {
  const std::string headers = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
  HttpRequest req { std::cref (logger) };
  Parser parser { req, { .headerCount = 1 } };
  EXPECT_EQ (parser.consume (headers + "0\r\nX: trailer\r\n\r\n").errorStatus, 431);
  HttpRequest large { std::cref (logger) };
  Parser trailers { large, { .headerBytes = 64 } };
  EXPECT_EQ (trailers.consume (headers + "0\r\nX: " + std::string (100, 'x') + "\r\n\r\n").errorStatus, 431);
  EXPECT_EQ (large.trailers.size(), 0);
}

TEST (RequestFraming, ambiguous_request_lengths_and_duplicate_host_are_rejected) {
  for (const auto fields : { "Content-Length: 1\r\nContent-Length: 2\r\n",
      "Content-Length: 1\r\nTransfer-Encoding: chunked\r\n", "Host: a\r\nHost: b\r\n" }) {
    HttpRequest req { std::cref (logger) };
    EXPECT_FALSE (req.parse (std::string { "POST / HTTP/1.1\r\n" } + fields + "\r\nx"));
  }
}

TEST (RequestFraming, rejects_bare_lf_and_split_malformed_delimiters) {
  HttpRequest req { std::cref (logger) };
  EXPECT_FALSE (req.parse ("GET / HTTP/1.1\nHost: localhost\n\n"));
  Parser parser { req };
  EXPECT_EQ (parser.consume ("GET / HTTP/1.1\r").status, Parser::Status::kIncomplete);
  EXPECT_EQ (parser.consume ("X: malformed\r\n\r\n").status, Parser::Status::kInvalid);
}

TEST (TransportSockets, head_limit_errors_have_no_wire_body) {
  App app; auto config = options();
  config.transport.limits.headerBytes = 80; config.transport.limits.bodyBytes = 1;
  app.start (config);
  for (const auto &wire : { std::string { "HEAD / HTTP/1.1\r\nContent-Length: 2\r\n\r\n" },
      "HEAD / HTTP/1.1\r\nX: " + std::string (100, 'x') }) {
    test::Client client { app.port() }; client.send (wire);
    const auto res = client.read (true);
    EXPECT_TRUE (res.status == 413 || res.status == 431);
    EXPECT_TRUE (client.waitForClose());
    EXPECT_FALSE (client.hasDataWithin (5ms));
  }
}

TEST (TransportSockets, receive_errors_return_specific_status_and_stop_the_pipeline) {
  auto config = options();
  config.transport.limits = { .headerBytes = 100, .targetBytes = 8, .headerCount = 2, .bodyBytes = 4 };
  App app;
  std::atomic<int> calls { 0 };
  app.use ([&] (auto &, auto &res, auto) { ++calls; res.send ("ok"); });
  app.start (config);
  const std::vector<std::pair<std::string, int>> cases {
    { "POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\n", 413 },
    { "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n", 413 },
    { get ("/12345678"), 414 },
    { "GET / HTTP/1.1\r\nX: " + std::string (120, 'x'), 431 },
    { "GET / HTTP/1.1\r\nA: 1\r\nB: 2\r\nC: 3\r\n\r\n", 431 },
    { "POST / HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 1\r\n\r\n", 417 },
    { "GET / HTTP/1.2\r\n\r\n", 505 }
  };
  for (const auto &[wire, status] : cases) {
    test::Client client { app.port() };
    client.send (wire + get());
    EXPECT_EQ (client.read().status, status) << wire;
    EXPECT_TRUE (client.waitForClose());
  }
  EXPECT_EQ (calls, 0);
  test::Client valid { app.port() }; valid.send (get());
  EXPECT_EQ (valid.read().body, "ok");
}

TEST (TransportSockets, body_limit_resets_for_each_pipelined_request) {
  auto config = options(); config.transport.limits.bodyBytes = 4;
  App app;
  app.post ("/", [] (const auto &req, auto &res) { res.send (std::string (req.body.begin(), req.body.end())); });
  app.start (config);
  test::Client client { app.port() };
  const std::string req = "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nabcd";
  client.send (req + req);
  EXPECT_EQ (client.read().body, "abcd"); EXPECT_EQ (client.read().body, "abcd");
}

TEST (ResponseFraming, derives_length_and_strips_unsupported_transfer_and_trailer_fields) {
  HttpResponse res;
  res.headers().set ("Content-Length", "999");
  res.headers().set ("Transfer-Encoding", "chunked");
  res.headers().set ("Trailer", "x");
  res.send ("hello");
  const auto wire = res.data();
  EXPECT_NE (wire.find ("content-length: 5\r\n"), std::string::npos);
  EXPECT_EQ (wire.find ("999"), std::string::npos);
  EXPECT_EQ (wire.find ("transfer-encoding"), std::string::npos);
  EXPECT_EQ (wire.find ("trailer:"), std::string::npos);
  EXPECT_EQ (body (res), "hello");
}

TEST (ResponseFraming, bodyless_statuses_discard_content_and_use_appropriate_length) {
  for (const auto status : { 204, 205, 304 }) {
    HttpResponse res;
    res.headers().set ("content-length", "7");
    res.status (status).send ("discard");
    EXPECT_TRUE (body (res).empty());
    EXPECT_EQ (res.data().find ("content-length:") != std::string::npos, status == 205);
    if (status == 205) EXPECT_NE (res.data().find ("content-length: 0\r\n"), std::string::npos);
  }
}

TEST (ResponseFraming, rejects_invalid_or_interim_final_status_without_changing_response) {
  for (const auto status : { 0u, 99u, 100u, 101u, 199u, 600u, 999u }) {
    HttpResponse res;
    EXPECT_THROW (res.status (status), std::invalid_argument);
    EXPECT_EQ (res.status(), 200);
  }
}

TEST (ResponseFraming, header_injection_is_rejected_at_assignment_and_serialization) {
  HttpResponse res;
  for (const auto name : { "", "bad name", "bad:name", "bad\r\nname" })
    EXPECT_THROW (res.headers().set (name, "value"), std::invalid_argument);
  for (const auto &value : { std::string { "ok\r\nx: bad" }, std::string { "a\0b", 3 }, std::string (1, '\x7f') })
    EXPECT_THROW (res.headers().set ("x", value), std::invalid_argument);
  res.headers().set ("x", "valid\tvalue");
  res.headers().begin()->second = "bypass\r\ninjection: yes";
  EXPECT_THROW (res.data(), std::invalid_argument);
}

TEST (TransportSockets, bodyless_responses_and_overridden_framing_preserve_pipeline_boundaries) {
  App app;
  for (const auto status : { 204, 205, 304 })
    app.get ("/" + std::to_string (status), [status] (const auto &, auto &res) { res.status (status).send ("discard"); });
  app.get ("/", [] (const auto &, auto &res) {
    res.headers().set ("content-length", "999");
    res.headers().set ("transfer-encoding", "chunked"); res.send ("ok");
  });
  app.start (options());
  test::Client client { app.port() };
  client.send (get ("/204") + get ("/205") + get ("/304") + get() + get());
  for (const auto status : { 204, 205, 304 }) EXPECT_EQ (client.read().status, status);
  EXPECT_EQ (client.read().body, "ok"); EXPECT_EQ (client.read().body, "ok");
}

TEST (TransportSockets, response_close_token_closes_and_http10_keep_alive_is_explicit) {
  App app;
  app.get ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  app.get ("/close", [] (const auto &, auto &res) {
    res.headers().set ("connection", "keep-alive, Close"); res.send ("closed");
  });
  app.start (options());
  test::Client client { app.port() };
  client.send ("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
  EXPECT_EQ (client.read().headers.at ("connection"), "keep-alive");
  client.send (get ("/close") + get());
  EXPECT_EQ (client.read().body, "closed");
  EXPECT_TRUE (client.waitForClose());
}

TEST (TransportSockets, invalid_response_header_becomes_a_framed_500) {
  App app;
  app.get ("/", [] (const auto &, auto &res) {
    res.headers().set ("x", "valid"); res.headers().begin()->second = "bad\r\ninjected: yes"; res.send ("discard");
  });
  app.start (options()); test::Client client { app.port() }; client.send (get());
  const auto res = client.read();
  EXPECT_EQ (res.status, 500); EXPECT_EQ (res.headers.count ("injected"), 0);
  EXPECT_TRUE (client.waitForClose());
}

TEST (MethodDefaults, head_uses_get_without_rewriting_method_and_respects_registration_order) {
  App app;
  app.get ("/", [] (const auto &req, auto &res) { EXPECT_EQ (req.method, HttpMethod::kHead); res.send ("get"); });
  app.head ("/", [] (const auto &, auto &res) { res.send ("later head"); });
  EXPECT_EQ (body (run (app, HttpMethod::kHead)), "get");
  App explicitHead;
  explicitHead.head ("/", [] (const auto &, auto &res) { res.send ("head"); });
  explicitHead.get ("/", [] (const auto &, auto &res) { res.send ("get"); });
  EXPECT_EQ (body (run (explicitHead, HttpMethod::kHead)), "head");
}

TEST (MethodDefaults, options_aggregates_nested_matching_routes_without_running_their_handlers) {
  App app; Router api; Router users;
  int middleware = 0;
  users.use ([&] (auto &, auto &, auto next) { ++middleware; next(); });
  users.get ("/:id", [] (const auto &, auto &) { FAIL(); });
  users.patch ("/:id", [] (const auto &, auto &) { FAIL(); });
  api.use ("/users", users); app.use ("/api", api);
  app.del ("/api/users/:id", [] (const auto &, auto &) { FAIL(); });
  app.post ("/elsewhere", [] (const auto &, auto &) { FAIL(); });
  const auto res = run (app, HttpMethod::kOptions, "/api/users/42");
  EXPECT_EQ (res.status(), 204);
  EXPECT_EQ (res.headers().get ("allow"), "GET, HEAD, DELETE, OPTIONS, PATCH");
  EXPECT_EQ (middleware, 1);
  EXPECT_EQ (run (app, HttpMethod::kOptions, "/missing").status(), 404);
  EXPECT_EQ (run (app, HttpMethod::kPost, "/api/users/42").status(), 404);
}

TEST (MethodDefaults, explicit_options_and_custom_fallback_override_automatic_response) {
  App app;
  app.get ("/", [] (const auto &, auto &) { FAIL(); });
  app.options ("/", [] (const auto &, auto &res) { res.send ("explicit"); });
  EXPECT_EQ (body (run (app, HttpMethod::kOptions)), "explicit");
  app.setDefault ([] (const auto &, auto &res) { res.status (403).end(); });
  EXPECT_EQ (run (app, HttpMethod::kOptions, "/missing").status(), 403);
  App fallback;
  fallback.get ("/", [] (const auto &, auto &) { FAIL(); });
  fallback.setDefault ([] (const auto &, auto &res) { res.status (403).end(); });
  EXPECT_EQ (run (fallback, HttpMethod::kOptions).status(), 403);
}

TEST (MethodDefaults, options_asterisk_lists_graph_methods_and_request_state_does_not_leak) {
  App app; Router child;
  child.post ("/:id", [] (const auto &, auto &) { FAIL(); });
  app.use ("/api", child);
  app.get ("/", [] (const auto &, auto &) { FAIL(); });
  EXPECT_EQ (run (app, HttpMethod::kOptions, "*").headers().get ("allow"), "GET, HEAD, POST, OPTIONS");
  EXPECT_EQ (run (app, HttpMethod::kOptions).headers().get ("allow"), "GET, HEAD, OPTIONS");
  child.patch ("/:id", [] (const auto &, auto &) { FAIL(); });
  EXPECT_EQ (run (app, HttpMethod::kOptions, "/api/x").headers().get ("allow"), "POST, OPTIONS, PATCH");
}

TEST (TransportSockets, implicit_head_and_options_have_valid_wire_framing) {
  App app;
  app.get ("/", [] (const auto &, auto &res) { res.send ("hello"); });
  app.start (options()); test::Client client { app.port() };
  client.send ("HEAD / HTTP/1.1\r\n\r\nOPTIONS / HTTP/1.1\r\n\r\n" + get());
  EXPECT_EQ (client.read (true).headers.at ("content-length"), "5");
  const auto opts = client.read(); EXPECT_EQ (opts.status, 204);
  EXPECT_EQ (opts.headers.at ("allow"), "GET, HEAD, OPTIONS");
  EXPECT_EQ (client.read().body, "hello");
}

TEST (TransportTimeouts, negative_values_are_rejected_before_startup_and_zero_disables) {
  for (int i = 0; i < 4; ++i) {
    App app; auto config = options();
    std::chrono::milliseconds *durations[] = { &config.transport.headerTimeout, &config.transport.bodyTimeout,
      &config.transport.writeTimeout, &config.transport.keepAliveTimeout };
    *durations[i] = -1ms;
    EXPECT_THROW (app.start (config), std::invalid_argument);
    EXPECT_FALSE (app.running());
  }
  App app; auto config = options();
  config.transport.headerTimeout = config.transport.bodyTimeout = config.transport.writeTimeout = config.transport.keepAliveTimeout = 0ms;
  app.get ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  app.start (config); test::Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\n");
  std::this_thread::sleep_for (100ms);
  client.send ("\r\n"); EXPECT_EQ (client.read().body, "ok");
}

TEST (TransportTimeouts, idle_new_connection_and_partial_headers_expire) {
  App app; auto config = options(); config.transport.headerTimeout = 100ms;
  app.start (config);
  test::Client idle { app.port() }; EXPECT_TRUE (idle.waitForClose());
  test::Client partial { app.port() }; partial.send ("GET / HTTP/1.1\r\nX:");
  EXPECT_TRUE (partial.waitForClose());
}

TEST (TransportTimeouts, body_deadline_expires_without_dispatching_partial_data) {
  App app; auto config = options(); config.transport.bodyTimeout = 100ms;
  std::atomic<int> calls { 0 };
  app.post ("/", [&] (const auto &, auto &res) { ++calls; res.end(); });
  app.start (config); test::Client client { app.port() };
  client.send ("POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nx");
  EXPECT_TRUE (client.waitForClose()); EXPECT_EQ (calls, 0);
}

TEST (TransportTimeouts, fragments_do_not_extend_the_absolute_header_deadline) {
  App app; auto config = options(); config.transport.headerTimeout = 250ms;
  app.start (config); test::Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\nX: a");
  std::this_thread::sleep_for (150ms);
  client.send ("b");
  std::this_thread::sleep_for (150ms);
  // A reset-on-progress timeout would still keep this connection open.
  const auto start = std::chrono::steady_clock::now();
  EXPECT_TRUE (client.waitForClose());
  EXPECT_LT (std::chrono::steady_clock::now() - start, 80ms);
}

TEST (TransportTimeouts, body_phase_gets_its_own_budget_and_old_timer_cannot_close_it) {
  App app; auto config = options();
  config.transport.headerTimeout = 200ms; config.transport.bodyTimeout = 600ms;
  app.post ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  app.start (config); test::Client client { app.port() };
  client.send ("POST / HTTP/1.1\r\nContent-Length: 1\r\n");
  std::this_thread::sleep_for (100ms); client.send ("\r\n");
  std::this_thread::sleep_for (200ms); client.send ("x");
  EXPECT_EQ (client.read().body, "ok");
}

TEST (TransportTimeouts, keep_alive_deadline_resets_after_each_completed_response) {
  App app; auto config = options(); config.transport.keepAliveTimeout = 300ms;
  app.get ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  app.start (config); test::Client client { app.port() };
  client.send (get()); EXPECT_EQ (client.read().body, "ok");
  std::this_thread::sleep_for (180ms);
  client.send (get()); EXPECT_EQ (client.read().body, "ok");
  std::this_thread::sleep_for (180ms);
  client.send (get()); EXPECT_EQ (client.read().body, "ok");
  EXPECT_TRUE (client.waitForClose());
}

TEST (TransportTimeouts, stalled_response_write_is_closed) {
  App app; auto config = options(); config.transport.writeTimeout = 50ms;
  app.get ("/", [] (const auto &, auto &res) { res.send (std::string (8 * 1024 * 1024, 'x')); });
  app.start (config); test::Client client { app.port() }; client.send (get());
  std::this_thread::sleep_for (200ms);
  EXPECT_TRUE (client.waitForClose());
}

TEST (TransportTimeouts, stopping_with_pending_read_and_timer_does_not_wait_for_deadline) {
  App app; auto config = options(); config.transport.headerTimeout = 60s;
  app.start (config); test::Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\n");
  auto stopped = std::async (std::launch::async, [&] { app.stop(); });
  EXPECT_EQ (stopped.wait_for (1s), std::future_status::ready);
  stopped.get(); EXPECT_TRUE (client.waitForClose());
}
}
