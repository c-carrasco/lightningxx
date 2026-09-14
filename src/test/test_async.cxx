// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <future>

#include <gtest/gtest.h>

#include <lightning/app.h>
#include <lightning/body_parser.h>
#include <lightning/testing.h>

#include "http_test_client.h"

namespace {

using namespace lightning;
using namespace std::chrono_literals;

Task<> pause (std::chrono::milliseconds duration = 1ms) {
  asio::steady_timer timer { co_await asio::this_coro::executor, duration };
  co_await timer.async_wait (asio::use_awaitable);
}

ServerOptions options (size_t workers = 1) {
  ServerOptions result;
  result.port = 0;
  result.workers = workers;
  result.logLevel = LogLevel::kFatal;
  return result;
}

struct Guard {
  std::atomic<int> &destroyed;
  ~Guard() { ++destroyed; }
};

template<class Routes, class Handler>
concept CanRegisterSync = requires (Routes &routes, Handler handler) { routes.get ("/", handler); };
static_assert (!CanRegisterSync<App, AsyncRequestHandler>);
static_assert (!CanRegisterSync<Router, AsyncRequestHandler>);
static_assert (!detail::RouteCallback<AsyncRequestHandler>);
using AsyncMiddleware = std::function<Task<> (HttpRequest &, HttpResponse &, Next)>;
static_assert (!detail::RouteCallback<AsyncMiddleware>);
template<class Routes>
concept CanRegisterAsyncMiddleware = requires (Routes &routes, AsyncMiddleware handler) { routes.use (handler); };
static_assert (!CanRegisterAsyncMiddleware<App>);
static_assert (!CanRegisterAsyncMiddleware<Router>);

template<class Routes>
void checkMethods() {
  using Helper = Routes & (Routes::*) (std::string_view, AsyncRequestHandler);
  const Helper helpers[] { &Routes::getAsync, &Routes::headAsync, &Routes::postAsync,
    &Routes::putAsync, &Routes::delAsync, &Routes::connectAsync, &Routes::optionsAsync,
    &Routes::traceAsync, &Routes::patchAsync };
  Routes routes;
  Logger logger { LogLevel::kFatal };
  for (int i = 0; i < kNumHttpMethods; ++i) {
    const auto path = "/" + std::to_string (i);
    const auto method = static_cast<HttpMethod> (i);
    EXPECT_EQ (&(routes.*helpers[i]) (path, [method] (auto &req, auto &res) -> Task<> {
      co_await pause(); EXPECT_EQ (req.method, method); res.send ("ok");
    }), &routes);
    HttpRequest req { std::cref (logger) };
    req.path = path; req.method = method;
    HttpResponse res;
    asio::io_context io;
    auto done = asio::co_spawn (io, routes.dispatchAsync (req, res), asio::use_future);
    io.run(); EXPECT_NO_THROW (done.get());
    EXPECT_TRUE (res.data().ends_with ("ok"));
  }
}

TEST (AsyncRoutes, app_method_helpers_register_every_method) { checkMethods<App>(); }
TEST (AsyncRoutes, router_method_helpers_register_every_method) { checkMethods<Router>(); }

TEST (AsyncRoutes, parses_body_query_and_nested_params_across_suspension) {
  App app;
  Router api;
  api.use (json());
  api.postAsync ("/:id", [] (auto &req, auto &res) -> Task<> {
    co_await pause();
    res.status (201).json ({ { "id", req.params.at ("id") }, { "base", req.baseUrl },
      { "path", req.path }, { "body", *req.jsonBody }, { "query", req.queryParams() } });
  });
  app.use ("/api/:tenant", api);
  HttpHeader headers;
  headers.set ("content-type", "application/json");
  const auto res = lightning::testing::Client { app }.post ("/api/shop/a%20b?q=yes", "{\"x\":1}", headers);
  EXPECT_EQ (res.status, 201);
  EXPECT_EQ (res.json().at ("id"), "a b");
  EXPECT_EQ (res.json().at ("base"), "/api/shop");
  EXPECT_EQ (res.json().at ("path"), "/a%20b");
  EXPECT_EQ (res.json().at ("body").at ("x"), 1);
  EXPECT_EQ (res.json().at ("query").at ("q").at (0), "yes");
}

TEST (AsyncRoutes, sync_middleware_unwinds_before_coroutine_and_retained_next_expires) {
  App app;
  std::vector<int> order;
  Next retained;
  app.use ([&] (auto &, auto &res, auto next) {
    retained = next;
    order.push_back (1); next(); order.push_back (2);
    res.headers().set ("x-before-async", "yes");
  });
  app.getAsync ("/", [&] (auto &, auto &res) -> Task<> {
    order.push_back (3);
    EXPECT_THROW (retained(), std::logic_error);
    co_await pause();
    order.push_back (4); res.send ("ok");
  });
  const auto res = lightning::testing::Client { app }.get ("/");
  EXPECT_EQ (order, (std::vector<int> { 1, 2, 3, 4 }));
  EXPECT_EQ (res.headers.get ("x-before-async"), "yes");
}

TEST (AsyncRoutes, middleware_can_short_circuit_and_post_next_failure_discards_selection) {
  for (int mode = 0; mode < 3; ++mode) {
    App app;
    app.use ([mode] (auto &, auto &res, auto next) {
      if (mode == 0) { res.status (401).end(); return; }
      next();
      if (mode == 1) throw std::runtime_error ("after next");
      res.send ("cannot complete a pending async response");
    });
    app.getAsync ("/", [] (auto &, auto &) -> Task<> { ADD_FAILURE(); co_return; });
    EXPECT_EQ (lightning::testing::Client { app }.get ("/").status, mode == 0 ? 401 : 500);
  }
}

TEST (AsyncRoutes, errors_before_after_await_and_after_send_enter_error_chain) {
  for (int mode = 0; mode < 3; ++mode) {
    App app;
    app.getAsync ("/:id", [mode] (auto &, auto &res) -> Task<> {
      if (mode > 0) co_await pause();
      if (mode == 2) res.send ("discard me");
      throw RequestParseError { RequestParseError::Code::kUnsupportedMediaType };
    });
    int errors = 0;
    app.onError ([&] (auto error, auto &req, auto &res, auto) {
      ++errors;
      EXPECT_EQ (req.params.at ("id"), "42");
      EXPECT_THROW (std::rethrow_exception (error), RequestParseError);
      res.status (422).send ("handled");
    });
    const auto res = lightning::testing::Client { app }.get ("/42");
    EXPECT_EQ (res.status, 422);
    EXPECT_EQ (res.body, "handled");
    EXPECT_EQ (errors, 1);
  }
}

TEST (AsyncRoutes, exceptions_while_creating_task_and_unhandled_errors_are_500) {
  App app;
  app.getAsync ("/factory", [] (auto &, auto &) -> Task<> { throw std::runtime_error ("factory"); });
  app.getAsync ("/empty", [] (auto &, auto &) -> Task<> { return {}; });
  app.getAsync ("/await", [] (auto &, auto &) -> Task<> { co_await pause(); throw std::runtime_error ("await"); });
  lightning::testing::Client client { app };
  EXPECT_EQ (client.get ("/factory").status, 500);
  EXPECT_EQ (client.get ("/empty").status, 500);
  EXPECT_EQ (client.get ("/await").body, "Internal server error");
}

TEST (AsyncRoutes, nested_errors_restore_each_parent_context_and_keep_error_order) {
  App app;
  Router child, parent;
  std::vector<int> order;
  child.getAsync ("/:id", [] (auto &, auto &) -> Task<> { co_await pause(); throw std::runtime_error ("fail"); });
  child.onError ([&] (auto, auto &req, auto &, auto next) {
    EXPECT_EQ (req.params.at ("id"), "42");
    EXPECT_EQ (req.baseUrl, "/api/shop/users"); order.push_back (1); next();
  });
  parent.use ("/users", child);
  parent.onError ([&] (auto, auto &req, auto &, auto) {
    EXPECT_EQ (req.params.at ("tenant"), "shop");
    EXPECT_FALSE (req.params.contains ("id"));
    EXPECT_EQ (req.path, "/users/42"); order.push_back (2); throw std::runtime_error ("replace");
  });
  app.use ("/api/:tenant", parent);
  app.onError ([&] (auto, auto &req, auto &res, auto) {
    EXPECT_EQ (req.path, "/api/shop/users/42"); EXPECT_TRUE (req.params.empty());
    order.push_back (3); res.status (503).end();
  });
  EXPECT_EQ (lightning::testing::Client { app }.get ("/api/shop/users/42").status, 503);
  EXPECT_EQ (order, (std::vector<int> { 1, 2, 3 }));
}

TEST (AsyncRoutes, router_local_errors_can_finish_without_parent_and_fallthrough_still_works) {
  App app;
  Router child;
  child.getAsync ("/fail", [] (auto &, auto &) -> Task<> { co_await pause(); throw std::runtime_error ("fail"); });
  child.onError ([] (auto, auto &, auto &res, auto) { res.status (409).end(); });
  app.use ("/api", child);
  app.getAsync ("/api/ok", [] (auto &, auto &res) -> Task<> { co_await pause(); res.send ("parent"); });
  app.onError ([] (auto, auto &, auto &, auto) { ADD_FAILURE(); });
  lightning::testing::Client client { app };
  EXPECT_EQ (client.get ("/api/fail").status, 409);
  EXPECT_EQ (client.get ("/api/ok").body, "parent");
  EXPECT_EQ (client.get ("/missing").status, 404);
}

TEST (AsyncRoutes, all_methods_and_empty_completion_use_existing_http_rules) {
  Router router;
  for (int i = 0; i < kNumHttpMethods; ++i) {
    const auto method = static_cast<HttpMethod> (i);
    router.addAsyncRoute (method, "/" + std::to_string (i), [method] (auto &req, auto &) -> Task<> {
      co_await pause(); EXPECT_EQ (req.method, method);
    });
  }
  lightning::testing::Client client { router };
  for (int i = 0; i < kNumHttpMethods; ++i) {
    if (i == static_cast<int> (HttpMethod::kConnect)) continue; // CONNECT requires authority form on the wire.
    EXPECT_EQ (client.request (static_cast<HttpMethod> (i), "/" + std::to_string (i)).status, 200);
  }
  router.getAsync ("/head", [] (auto &, auto &res) -> Task<> { co_await pause(); res.send ("hello"); });
  const auto head = client.request (HttpMethod::kHead, "/head");
  EXPECT_TRUE (head.body.empty()); EXPECT_EQ (head.headers.get ("content-length"), "5");
  const auto opt = client.request (HttpMethod::kOptions, "/head");
  EXPECT_EQ (opt.status, 204); EXPECT_EQ (opt.headers.get ("allow"), "GET, HEAD, OPTIONS");
}

TEST (AsyncRoutes, mutable_handler_identity_is_preserved_between_requests) {
  App app;
  app.getAsync ("/", [count = 0] (auto &, auto &res) mutable -> Task<> {
    co_await pause(); res.send (std::to_string (++count));
  });
  lightning::testing::Client client { app };
  EXPECT_EQ (client.get ("/").body, "1");
  EXPECT_EQ (client.get ("/").body, "2");
}

TEST (AsyncRoutes, snapshot_and_callback_lifetime_survive_dispatcher_destruction) {
  Logger logger { LogLevel::kFatal };
  HttpRequest req { std::cref (logger) };
  ASSERT_TRUE (req.parse ("GET /42 HTTP/1.1\r\n\r\n"));
  HttpResponse res;
  auto app = std::make_unique<App>();
  app->getAsync ("/:id", [owned = std::string (200, 'x')] (auto &request, auto &response) -> Task<> {
    co_await pause(); response.send (owned + request.params.at ("id"));
  });
  auto task = app->dispatchAsync (req, res);
  app.reset();
  asio::io_context io;
  auto result = asio::co_spawn (io, std::move (task), asio::use_future);
  io.run();
  EXPECT_NO_THROW (result.get());
  EXPECT_TRUE (res.data().ends_with (std::string (200, 'x') + "42"));
  EXPECT_TRUE (req.params.empty()); EXPECT_EQ (req.path, "/42");
}

TEST (AsyncRoutes, registration_changes_after_snapshot_apply_to_next_request) {
  App app;
  app.getAsync ("/", [&] (auto &, auto &) -> Task<> {
    app.onError ([] (auto, auto &, auto &res, auto) { res.status (409).end(); });
    co_await pause(); throw std::runtime_error ("fail");
  });
  lightning::testing::Client client { app };
  EXPECT_EQ (client.get ("/").status, 500);
  EXPECT_EQ (client.get ("/").status, 409);
}

TEST (AsyncRoutes, validates_registration_and_sync_dispatch_does_not_drop_coroutines) {
  App app;
  EXPECT_THROW (app.getAsync ("/", {}), std::invalid_argument);
  EXPECT_THROW (app.addAsyncRoute (HttpMethod::kUnknown, "/", [] (auto &, auto &) -> Task<> { co_return; }), std::invalid_argument);
  EXPECT_THROW (app.getAsync ("bad", [] (auto &, auto &) -> Task<> { co_return; }), std::invalid_argument);
  app.getAsync ("/", [] (auto &, auto &) -> Task<> { ADD_FAILURE(); co_return; });
  Logger logger { LogLevel::kFatal };
  HttpRequest req { std::cref (logger) };
  ASSERT_TRUE (req.parse ("GET / HTTP/1.1\r\n\r\n"));
  HttpResponse res;
  app.dispatch (req, res);
  EXPECT_EQ (res.status(), 500);
}

TEST (AsyncTransport, suspended_request_does_not_block_a_single_worker) {
  std::atomic<bool> release { false };
  std::promise<void> entered;
  auto ready = entered.get_future();
  App app;
  app.getAsync ("/slow", [&] (auto &, auto &res) -> Task<> {
    entered.set_value();
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!release && std::chrono::steady_clock::now() < deadline) co_await pause();
    res.send ("slow");
  });
  app.get ("/fast", [] (const auto &, auto &res) { res.send ("fast"); });
  app.start (options());
  lightning::test::Client slow { app.port() }, fast { app.port() };
  slow.send ("GET /slow HTTP/1.1\r\n\r\n");
  ASSERT_EQ (ready.wait_for (1s), std::future_status::ready);
  fast.send ("GET /fast HTTP/1.1\r\n\r\n");
  EXPECT_EQ (fast.read().body, "fast");
  EXPECT_FALSE (slow.hasDataWithin (10ms));
  release = true;
  EXPECT_EQ (slow.read().body, "slow");
}

TEST (AsyncTransport, keeps_pipelined_responses_ordered_and_original_head_framing) {
  App app;
  app.getAsync ("/:id", [] (auto &req, auto &res) -> Task<> {
    co_await pause (5ms);
    const auto id = req.params.at ("id");
    req.method = HttpMethod::kGet;
    if (id == "fail") throw std::runtime_error ("fail");
    res.send (id);
  });
  app.start (options (3));
  lightning::test::Client client { app.port() };
  client.send ("HEAD /first HTTP/1.1\r\n\r\nGET /second HTTP/1.1\r\n\r\nHEAD /fail HTTP/1.1\r\n\r\n");
  EXPECT_EQ (client.read (true).headers.at ("content-length"), "5");
  EXPECT_EQ (client.read().body, "second");
  EXPECT_EQ (client.read (true).status, 500);
  EXPECT_TRUE (client.waitForClose());
}

TEST (AsyncTransport, handler_deadline_cancels_await_and_destroys_locals_without_error_dispatch) {
  std::atomic<int> destroyed { 0 }, resumed { 0 }, errors { 0 };
  App app;
  app.getAsync ("/", [&] (auto &, auto &) -> Task<> {
    Guard guard { destroyed }; co_await pause (10s); ++resumed;
  });
  app.onError ([&] (auto, auto &, auto &, auto) { ++errors; });
  auto config = options (3);
  config.transport.handlerTimeout = 30ms;
  app.start (config);
  lightning::test::Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\n\r\n");
  EXPECT_TRUE (client.waitForClose());
  app.stop();
  EXPECT_EQ (destroyed, 1); EXPECT_EQ (resumed, 0); EXPECT_EQ (errors, 0);
}

TEST (AsyncTransport, shutdown_cancels_suspended_handlers_and_app_can_restart) {
  std::atomic<int> destroyed { 0 }, resumed { 0 };
  std::promise<void> entered;
  auto ready = entered.get_future();
  App app;
  app.getAsync ("/wait", [&] (auto &, auto &) -> Task<> {
    Guard guard { destroyed }; entered.set_value(); co_await pause (10s); ++resumed;
  });
  app.get ("/ok", [] (const auto &, auto &res) { res.send ("ok"); });
  app.start (options (3));
  lightning::test::Client client { app.port() };
  client.send ("GET /wait HTTP/1.1\r\n\r\n");
  ASSERT_EQ (ready.wait_for (1s), std::future_status::ready);
  const auto before = std::chrono::steady_clock::now();
  app.stop();
  EXPECT_LT (std::chrono::steady_clock::now() - before, 1s);
  EXPECT_EQ (destroyed, 1); EXPECT_EQ (resumed, 0);
  EXPECT_TRUE (client.waitForClose());
  app.start (options());
  lightning::test::Client restarted { app.port() };
  restarted.send ("GET /ok HTTP/1.1\r\n\r\n");
  EXPECT_EQ (restarted.read().body, "ok");
}

TEST (AsyncTransport, peer_disconnect_keeps_request_alive_until_completion) {
  std::atomic<int> destroyed { 0 };
  std::promise<void> entered, completed;
  auto ready = entered.get_future(); auto done = completed.get_future();
  App app;
  app.getAsync ("/:id", [&] (auto &req, auto &res) -> Task<> {
    Guard guard { destroyed }; entered.set_value(); co_await pause (20ms);
    EXPECT_EQ (req.params.at ("id"), "42"); res.send ("ok"); completed.set_value();
  });
  app.start (options());
  lightning::test::Client client { app.port() };
  client.send ("GET /42 HTTP/1.1\r\n\r\n");
  ASSERT_EQ (ready.wait_for (1s), std::future_status::ready);
  client.close();
  ASSERT_EQ (done.wait_for (1s), std::future_status::ready);
  app.stop(); EXPECT_EQ (destroyed, 1);
}

TEST (AsyncTransport, zero_handler_timeout_and_client_half_close_allow_response) {
  App app;
  auto config = options();
  config.transport.handlerTimeout = -1ms;
  EXPECT_THROW (app.start (config), std::invalid_argument);
  config.transport.handlerTimeout = 0ms;
  app.getAsync ("/", [] (auto &, auto &res) -> Task<> { co_await pause (20ms); res.send ("ok"); });
  app.start (config);
  lightning::test::Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\n\r\n"); client.finishSending();
  EXPECT_EQ (client.read().body, "ok");
  EXPECT_TRUE (client.waitForClose());
}

TEST (AsyncTransport, shutdown_releases_frames_even_if_user_disables_cancellation) {
  std::atomic<int> destroyed { 0 };
  std::promise<void> entered;
  auto ready = entered.get_future();
  App app;
  app.getAsync ("/", [&] (auto &, auto &) -> Task<> {
    Guard guard { destroyed };
    co_await asio::this_coro::reset_cancellation_state (asio::disable_cancellation());
    entered.set_value(); co_await pause (10s);
    ADD_FAILURE() << "Shutdown must not wait for the timer";
  });
  app.start (options());
  lightning::test::Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\n\r\n");
  ASSERT_EQ (ready.wait_for (1s), std::future_status::ready);
  app.stop();
  EXPECT_EQ (destroyed, 1);
}

TEST (AsyncRoutes, dispatch_validates_response_and_unknown_method) {
  Dispatcher routes;
  Logger logger { LogLevel::kFatal };
  HttpRequest req { std::cref (logger) };
  HttpResponse res;
  asio::io_context io;
  auto unknown = asio::co_spawn (io, routes.dispatchAsync (req, res), asio::use_future);
  io.run(); EXPECT_NO_THROW (unknown.get()); EXPECT_EQ (res.status(), 404);
  io.restart();
  auto finished = asio::co_spawn (io, routes.dispatchAsync (req, res), asio::use_future);
  io.run(); EXPECT_THROW (finished.get(), std::logic_error);
}

TEST (AsyncRoutes, error_handlers_can_replace_and_forward_without_reentry) {
  App app;
  int first = 0, second = 0;
  app.getAsync ("/", [] (auto &, auto &) -> Task<> { co_await pause(); throw RouteDecodeError {}; });
  app.onError ([&] (auto, auto &, auto &, auto next) {
    ++first; next(); throw RequestParseError { RequestParseError::Code::kTooLarge };
  });
  app.onError ([&] (auto, auto &, auto &, auto next) { ++second; next(); });
  EXPECT_EQ (lightning::testing::Client { app }.get ("/").status, 413);
  EXPECT_EQ (first, 1); EXPECT_EQ (second, 1);
}

TEST (AsyncRoutes, local_error_handler_sees_mutations_before_failure) {
  App app;
  app.getAsync ("/:id", [] (auto &req, auto &) -> Task<> {
    co_await pause(); req.params["id"] = "changed"; req.path = "/rewritten";
    throw std::runtime_error ("fail");
  });
  app.onError ([] (auto, auto &req, auto &res, auto) {
    EXPECT_EQ (req.path, "/rewritten"); res.send (req.params.at ("id"));
  });
  EXPECT_EQ (lightning::testing::Client { app }.get ("/42").body, "changed");
}
}
