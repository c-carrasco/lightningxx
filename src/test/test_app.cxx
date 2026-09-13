// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <future>
#include <limits>
#include <stdexcept>
#include <gtest/gtest.h>
#include <lightning/app.h>
#include "http_test_client.h"

namespace {
using namespace std::chrono_literals;
using lightning::App;
using lightning::HttpMethod;
using lightning::HttpRequest;
using lightning::HttpResponse;
using lightning::RequestHandler;
using lightning::ServerOptions;
using lightning::test::Client;

lightning::Logger logger { lightning::LogLevel::kFatal };

ServerOptions localOptions() {
  ServerOptions options;
  options.port = 0;
  options.logLevel = lightning::LogLevel::kFatal;
  return options;
}

std::string get (std::string_view path) {
  return "GET " + std::string (path) + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

HttpResponse dispatch (const App &app, HttpMethod method, std::string_view path) {
  HttpRequest request { std::cref (logger) };
  request.method = method;
  request.path = path;
  HttpResponse response;
  app.dispatch (request, response);
  return response;
}

std::string body (const HttpResponse &response) {
  const auto wire = response.data();
  return wire.substr (wire.find ("\r\n\r\n") + 4);
}

// ----------------------------------------------------------------------------
// App tests
// ----------------------------------------------------------------------------

TEST (App, configuration_and_dispatch_do_not_start_transport) {
  App app;
  EXPECT_EQ (&app.get ("/hello", [] (const auto &, auto &res) { res.send ("hello"); }), &app);
  EXPECT_FALSE (app.running());
  EXPECT_EQ (app.port(), 0);
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/hello")), "hello");
  EXPECT_FALSE (app.running());
  app.stop();
  app.stop();
}

using MethodHelper = App & (App::*) (std::string_view, RequestHandler);
class AppMethods: public ::testing::TestWithParam<std::pair<HttpMethod, MethodHelper>> {};

TEST_P (AppMethods, helper_registers_only_its_http_method) {
  const auto [method, helper] = GetParam();
  App app;
  EXPECT_EQ (&(app.*helper) ("/resource", [] (const auto &, auto &res) {
    res.status (201).send ("matched");
  }), &app);
  for (int i = 0; i < lightning::kNumHttpMethods; ++i) {
    const auto candidate = static_cast<HttpMethod> (i);
    const auto response = dispatch (app, candidate, "/resource");
    EXPECT_EQ (body (response), candidate == method ? "matched" : "Not found");
  }
}

INSTANTIATE_TEST_SUITE_P (AllMethods, AppMethods, ::testing::Values (
  std::make_pair (HttpMethod::kGet, static_cast<MethodHelper> (&App::get)), std::make_pair (HttpMethod::kHead, static_cast<MethodHelper> (&App::head)),
  std::make_pair (HttpMethod::kPost, static_cast<MethodHelper> (&App::post)), std::make_pair (HttpMethod::kPut, static_cast<MethodHelper> (&App::put)),
  std::make_pair (HttpMethod::kDelete, static_cast<MethodHelper> (&App::del)), std::make_pair (HttpMethod::kConnect, static_cast<MethodHelper> (&App::connect)),
  std::make_pair (HttpMethod::kOptions, static_cast<MethodHelper> (&App::options)), std::make_pair (HttpMethod::kTrace, static_cast<MethodHelper> (&App::trace)),
  std::make_pair (HttpMethod::kPatch, static_cast<MethodHelper> (&App::patch))));

TEST (App, exact_routes_keep_registration_order_and_default_is_resettable) {
  App app;
  EXPECT_EQ (&app.addRoute (HttpMethod::kGet, "/users", [] (const auto &, auto &res) { res.send ("first"); }), &app);
  app.get ("/users", [] (const auto &, auto &res) { res.send ("second"); });
  EXPECT_EQ (&app.setDefault ([] (const auto &, auto &res) { res.status (410).send ("gone"); }), &app);
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/users")), "first");
  for (const auto path : { "/users/", "/Users", "/users/1" })
    EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, path)), "gone");
  EXPECT_EQ (body (dispatch (app, HttpMethod::kPost, "/users")), "gone");
  app.setDefault ({});
  EXPECT_EQ (dispatch (app, HttpMethod::kGet, "/missing").data().substr (0, 12), "HTTP/1.1 404");
}

TEST (App, registration_owns_paths_and_handlers_and_honors_view_lengths) {
  App app;
  {
    std::string path = "/" + std::string (256, 'x');
    std::string value = "owned";
    app.get (path, [value] (const auto &, auto &res) { res.send (value); });
    path.assign (path.size(), 'z');
  }
  app.get (std::string_view ("/slice-extra", 6), [] (const auto &, auto &res) { res.send ("slice"); });
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/" + std::string (256, 'x'))), "owned");
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/slice")), "slice");
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/slice-extra")), "Not found");
}

TEST (App, dispatch_uses_parsed_path_and_preserves_query_and_binary_body) {
  App app;
  app.post ("/echo", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.query, "q=1");
    res.send (std::string (req.body.begin(), req.body.end()));
  });
  HttpRequest request { std::cref (logger) };
  const std::string bytes { "a\0b", 3 };
  ASSERT_TRUE (request.parse ("POST /echo?q=1 HTTP/1.1\r\nContent-Length: 3\r\n\r\n" + bytes));
  HttpResponse response;
  app.dispatch (request, response);
  EXPECT_EQ (body (response), bytes);
}

TEST (App, rejects_invalid_registration_without_changing_existing_routes) {
  App app;
  app.get ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  EXPECT_THROW (app.get ("/", {}), std::invalid_argument);
  EXPECT_THROW (app.addRoute (HttpMethod::kUnknown, "/", [] (const auto &, auto &) {}), std::invalid_argument);
  EXPECT_THROW (app.addRoute (static_cast<HttpMethod> (99), "/", [] (const auto &, auto &) {}), std::invalid_argument);
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/")), "ok");
  EXPECT_EQ (body (dispatch (app, HttpMethod::kUnknown, "/")), "Not found");
}

TEST (App, dispatch_handles_exceptions_and_releases_configuration_lock) {
  App app;
  app.get ("/register", [&app] (const auto &, auto &res) {
    app.get ("/new", [] (const auto &, auto &next) { next.send ("new"); });
    app.setDefault ([] (const auto &, auto &) { throw std::runtime_error ("fallback"); });
    res.send ("registered");
  });
  app.get ("/throw", [] (const auto &, auto &) { throw 42; });
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/register")), "registered");
  EXPECT_EQ (body (dispatch (app, HttpMethod::kGet, "/new")), "new");
  EXPECT_EQ (dispatch (app, HttpMethod::kGet, "/throw").status(), 500);
  EXPECT_EQ (dispatch (app, HttpMethod::kGet, "/missing").status(), 500);
}

TEST (App, dispatch_and_registration_can_run_concurrently) {
  App app;
  app.get ("/ok", [] (const auto &, auto &res) { res.send ("ok"); });
  std::atomic<int> failures { 0 };
  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back ([&] {
      for (int j = 0; j < 100; ++j) {
        if (body (dispatch (app, HttpMethod::kGet, "/ok")) != "ok") ++failures;
        const auto fallback = body (dispatch (app, HttpMethod::kGet, "/missing"));
        if (fallback != "Not found" && fallback != "gone") ++failures;
      }
    });
  }
  for (int i = 0; i < 100; ++i) {
    app.get ("/dynamic/" + std::to_string (i), [] (const auto &, auto &res) { res.send ("dynamic"); });
    app.setDefault ([] (const auto &, auto &res) { res.send ("gone"); });
  }
  for (auto &reader : readers) reader.join();
  EXPECT_EQ (failures, 0);
}

TEST (App, start_serves_preconfigured_routes_and_allows_later_registration) {
  App app;
  app.get ("/ready", [&app] (const auto &, auto &res) {
    EXPECT_TRUE (app.running());
    EXPECT_NE (app.port(), 0);
    res.send ("ready");
  });
  EXPECT_EQ (&app.start (localOptions()), &app);
  EXPECT_TRUE (app.running());
  ASSERT_NE (app.port(), 0);
  Client client { app.port() };
  client.send (get ("/ready"));
  EXPECT_EQ (client.read().body, "ready");
  app.post ("/later", [] (const auto &, auto &res) { res.status (201).send ("created"); });
  client.send ("POST /later HTTP/1.1\r\nContent-Length: 0\r\n\r\n");
  const auto response = client.read();
  EXPECT_EQ (response.status, 201);
  EXPECT_EQ (response.body, "created");
  app.stop();
  EXPECT_FALSE (app.running());
  EXPECT_EQ (app.port(), 0);
  EXPECT_TRUE (client.waitForClose());
}

TEST (App, duplicate_start_is_rejected_and_stop_allows_restart_with_routes_retained) {
  App app;
  app.get ("/ok", [] (const auto &, auto &res) { res.send ("ok"); });
  app.start (0);
  const auto port = app.port();
  EXPECT_THROW (app.start (0), std::logic_error);
  EXPECT_THROW (app.listen (0), std::logic_error);
  EXPECT_EQ (app.port(), port);
  app.stop();
  app.stop();
  // Rebinding the same port also verifies that stop released the listening socket.
  app.start (port);
  Client client { app.port() };
  client.send (get ("/ok"));
  EXPECT_EQ (client.read().body, "ok");
}

TEST (App, invalid_options_leave_application_reusable) {
  App app;
  auto options = localOptions();
  options.workers = 0;
  EXPECT_THROW (app.start (options), std::invalid_argument);
  EXPECT_FALSE (app.running());
  EXPECT_EQ (app.port(), 0);
  options.workers = std::numeric_limits<size_t>::max();
  EXPECT_THROW (app.start (options), std::length_error);
  EXPECT_FALSE (app.running());
  options.workers = 1;
  options.address = "invalid-address";
  EXPECT_THROW (app.start (options), std::system_error);
  EXPECT_FALSE (app.running());
  app.start (localOptions());
  EXPECT_TRUE (app.running());
}

TEST (App, occupied_port_failure_does_not_discard_configuration) {
  App first;
  first.start (localOptions());
  App second;
  second.get ("/ok", [] (const auto &, auto &res) { res.send ("second"); });
  EXPECT_THROW (second.start (first.port()), std::system_error);
  EXPECT_FALSE (second.running());
  second.start (localOptions());
  Client client { second.port() };
  client.send (get ("/ok"));
  EXPECT_EQ (client.read().body, "second");
}

TEST (App, supports_explicit_ipv6_listening_address) {
  App app;
  auto options = localOptions();
  options.address = "::1";
  app.get ("/ok", [] (const auto &req, auto &res) { res.send (req.ip); });
  app.start (options);
  Client client { app.port(), "::1" };
  client.send (get ("/ok"));
  EXPECT_EQ (client.read().body, "::1");
}

TEST (App, listen_blocks_until_stop_and_can_be_used_again) {
  App app;
  for (int i = 0; i < 2; ++i) {
    auto listener = std::async (std::launch::async, [&] { app.listen (localOptions()); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!app.running() && listener.wait_for (0ms) != std::future_status::ready &&
        std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for (1ms);
    EXPECT_TRUE (app.running());
    EXPECT_EQ (listener.wait_for (20ms), std::future_status::timeout);
    app.stop();
    // A new run must not prolong the previous listen() call.
    app.start (localOptions());
    EXPECT_EQ (listener.wait_for (1s), std::future_status::ready);
    listener.get();
    app.stop();
    EXPECT_FALSE (app.running());
  }
}

TEST (App, stop_waits_for_handlers_without_holding_app_mutex) {
  App app;
  std::promise<void> entered;
  std::promise<void> release;
  auto released = release.get_future().share();
  app.get ("/wait", [&] (const auto &, auto &res) {
    entered.set_value();
    EXPECT_EQ (released.wait_for (2s), std::future_status::ready);
    EXPECT_FALSE (app.running());
    EXPECT_EQ (app.port(), 0);
    res.send ("done");
  });
  app.start (localOptions());
  Client client { app.port() };
  client.send (get ("/wait"));
  EXPECT_EQ (entered.get_future().wait_for (1s), std::future_status::ready);
  auto stopping = std::async (std::launch::async, [&] { app.stop(); });
  const auto deadline = std::chrono::steady_clock::now() + 1s;
  while (app.running() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for (1ms);
  EXPECT_FALSE (app.running());
  EXPECT_EQ (stopping.wait_for (20ms), std::future_status::timeout);
  auto alsoStopping = std::async (std::launch::async, [&] { app.stop(); });
  EXPECT_EQ (alsoStopping.wait_for (20ms), std::future_status::timeout);
  EXPECT_THROW (app.start (localOptions()), std::logic_error);
  release.set_value();
  EXPECT_EQ (stopping.wait_for (1s), std::future_status::ready);
  stopping.get();
  EXPECT_EQ (alsoStopping.wait_for (1s), std::future_status::ready);
  alsoStopping.get();
  EXPECT_TRUE (client.waitForClose());
}

TEST (App, failed_listen_does_not_block_or_prevent_subsequent_start) {
  App app;
  auto options = localOptions();
  options.workers = 0;
  EXPECT_THROW (app.listen (options), std::invalid_argument);
  EXPECT_FALSE (app.running());
  app.start (localOptions());
  EXPECT_TRUE (app.running());
}

TEST (App, destruction_closes_idle_and_partial_connections) {
  auto app = std::make_unique<App>();
  app->get ("/ok", [] (const auto &, auto &res) { res.send ("ok"); });
  app->start (localOptions());
  Client idle { app->port() };
  idle.send (get ("/ok"));
  EXPECT_EQ (idle.read().body, "ok");
  Client partial { app->port() };
  partial.send ("GET /ok HTTP/1.1\r\n");
  app.reset();
  EXPECT_TRUE (idle.waitForClose());
  EXPECT_TRUE (partial.waitForClose());
}

TEST (App, socket_dispatch_preserves_exception_boundary_and_head_framing) {
  App app;
  app.get ("/throw", [] (const auto &, auto &res) {
    res.headers().set ("x-partial", "discard");
    res.send ("partial");
    throw std::runtime_error ("private details");
  });
  app.head ("/ok", [] (const auto &, auto &res) { res.send ("ok"); });
  app.get ("/ok", [] (const auto &, auto &res) { res.send ("ok"); });
  app.setDefault ([] (const auto &, auto &) { throw 42; });
  app.start (localOptions());
  for (const auto path : { "/throw", "/missing" }) {
    Client client { app.port() };
    client.send (get (path));
    const auto response = client.read();
    EXPECT_EQ (response.status, 500);
    EXPECT_EQ (response.body, "Internal server error");
    EXPECT_EQ (response.headers.count ("x-partial"), 0);
    EXPECT_TRUE (client.waitForClose());
  }
  Client client { app.port() };
  client.send ("HEAD /ok HTTP/1.1\r\n\r\n" + get ("/ok"));
  EXPECT_EQ (client.read (true).headers.at ("content-length"), "2");
  EXPECT_EQ (client.read().body, "ok");
}

}
