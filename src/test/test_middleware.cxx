// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <future>

#include <gtest/gtest.h>

#include <lightning/app.h>
#include "http_test_client.h"

namespace {

using namespace std::chrono_literals;
using lightning::App;
using lightning::HttpMethod;
using lightning::HttpRequest;
using lightning::HttpResponse;
using lightning::Next;
using lightning::test::Client;
lightning::Logger logger { lightning::LogLevel::kFatal };

HttpResponse run (const App &app, std::string_view path = "/", HttpMethod method = HttpMethod::kGet) {
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

lightning::ServerOptions localOptions() {
  lightning::ServerOptions options;
  options.port = 0;
  options.workers = 3;
  options.logLevel = lightning::LogLevel::kFatal;
  return options;
}

std::string get (std::string_view path) {
  return "GET " + std::string (path) + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

TEST (Middleware, executes_in_registration_order_and_unwinds_after_next) {
  App app;
  std::vector<int> order;
  EXPECT_EQ (&app.use ([&] (auto &, auto &res, auto next) {
    order.push_back (1);
    next();
    EXPECT_TRUE (res.finished());
    EXPECT_EQ (res.status(), 201);
    order.push_back (5);
  }), &app);
  app.use ([&] (auto &, auto &, auto next) { order.push_back (2); next(); order.push_back (4); });
  app.get ("/", [&] (const auto &, auto &res) { order.push_back (3); res.status (201).send ("ok"); });
  app.use ([&] (auto &, auto &, auto next) { order.push_back (99); next(); });
  EXPECT_EQ (body (run (app)), "ok");
  EXPECT_EQ (order, (std::vector<int> { 1, 2, 3, 4, 5 }));
}

TEST (Middleware, route_chains_fall_through_to_later_matching_routes) {
  App app;
  std::vector<int> order;
  app.get ("/", [&] (auto &, auto &, auto next) { order.push_back (1); next(); },
    [&] (auto &, auto &, auto next) { order.push_back (2); next(); });
  app.use ([&] (auto &, auto &, auto next) { order.push_back (3); next(); });
  app.get ("/", [&] (auto &, auto &, auto next) { order.push_back (4); next(); },
    [&] (const auto &, auto &res) { order.push_back (5); res.send ("done"); });
  EXPECT_EQ (body (run (app)), "done");
  EXPECT_EQ (order, (std::vector<int> { 1, 2, 3, 4, 5 }));
}

TEST (Middleware, terminal_route_handler_ends_empty_responses_and_stops_chain) {
  App app;
  bool later = false;
  app.get ("/", [] (const auto &, auto &res) { res.status (202); },
    [&] (auto &, auto &, auto next) { later = true; next(); });
  const auto response = run (app);
  EXPECT_EQ (response.status(), 202);
  EXPECT_TRUE (response.finished());
  EXPECT_TRUE (body (response).empty());
  EXPECT_FALSE (later);
}

TEST (Middleware, early_response_skips_routes_and_fallback) {
  App app;
  app.use ([] (auto &, auto &res, auto) { res.status (401).send ("unauthorized"); });
  app.get ("/", [] (const auto &, auto &) { FAIL() << "Route should be skipped"; });
  app.setDefault ([] (const auto &, auto &) { FAIL() << "Fallback should be skipped"; });
  EXPECT_EQ (run (app).status(), 401);
  EXPECT_EQ (run (app, "/missing").status(), 401);
}

TEST (Middleware, end_completes_an_empty_middleware_response) {
  App app;
  app.use ([] (auto &, auto &res, auto) { res.status (202).end(); });
  const auto response = run (app);
  EXPECT_TRUE (response.finished());
  EXPECT_EQ (response.status(), 202);
  EXPECT_EQ (body (response), "");
}

TEST (Middleware, response_headers_can_be_decorated_while_unwinding) {
  App app;
  app.use ([] (auto &, auto &res, auto next) { next(); res.headers().set ("x-outer", "added"); });
  app.get ("/", [] (const auto &, auto &res) { res.send ("done"); });
  const auto response = run (app);
  EXPECT_EQ (body (response), "done");
  EXPECT_EQ (response.headers().get ("x-outer"), "added");
}

TEST (Middleware, prefix_matching_obeys_segment_boundaries_and_normalizes_trailing_slash) {
  App app;
  EXPECT_EQ (&app.use ("/api/", [] (auto &, auto &res, auto next) {
    res.headers().set ("x-prefix", "matched"); next();
  }), &app);
  for (const auto path : { "/api", "/api/", "/api/users" }) {
    const auto response = run (app, path);
    EXPECT_EQ (response.headers().get ("x-prefix"), "matched");
    EXPECT_EQ (response.status(), 404);
  }
  for (const auto path : { "/apiculture", "/API", "/other/api", "/api%2fusers" })
    EXPECT_FALSE (run (app, path).headers().contains ("x-prefix"));
}

TEST (Middleware, prefix_is_owned_and_view_length_is_respected) {
  App app;
  std::string prefix = "/" + std::string (200, 'x');
  const auto original = prefix;
  app.use (prefix, [] (auto &, auto &res, auto) { res.send ("owned"); });
  prefix.assign (prefix.size(), 'z');
  app.use (std::string_view ("/slice-extra", 6), [] (auto &, auto &res, auto) { res.send ("slice"); });
  EXPECT_EQ (body (run (app, original + "/child")), "owned");
  EXPECT_EQ (body (run (app, "/slice")), "slice");
  EXPECT_EQ (run (app, "/slice-extra").status(), 404);
}

TEST (Middleware, method_specific_chains_do_not_intercept_other_methods) {
  App app;
  app.post ("/", [] (auto &, auto &res, auto) { res.status (201).send ("post"); });
  app.get ("/", [] (auto &, auto &, auto next) { next(); },
    [] (const auto &, auto &res) { res.send ("get"); });
  EXPECT_EQ (body (run (app)), "get");
  EXPECT_EQ (body (run (app, "/", HttpMethod::kPost)), "post");
  EXPECT_EQ (run (app, "/", HttpMethod::kPut).status(), 404);
}

using ChainHelper = App & (App::*) (std::string_view, lightning::Middleware, lightning::RequestHandler);
class MiddlewareMethods: public ::testing::TestWithParam<std::pair<HttpMethod, ChainHelper>> {};

TEST_P (MiddlewareMethods, every_method_helper_accepts_middleware_and_a_terminal_handler) {
  App app;
  const auto [method, helper] = GetParam();
  int calls = 0;
  (app.*helper) ("/", [&] (auto &, auto &, auto next) { ++calls; next(); },
    [] (const auto &, auto &res) { res.send ("matched"); });
  for (int i = 0; i < lightning::kNumHttpMethods; ++i) {
    const auto candidate = static_cast<HttpMethod> (i);
    const bool matched = candidate == method || (method == HttpMethod::kGet && candidate == HttpMethod::kHead);
    EXPECT_EQ (run (app, "/", candidate).status(), matched ? 200 : candidate == HttpMethod::kOptions ? 204 : 404);
  }
  EXPECT_EQ (calls, method == HttpMethod::kGet ? 2 : 1);
}

INSTANTIATE_TEST_SUITE_P (AllMethods, MiddlewareMethods, ::testing::Values (
  std::make_pair (HttpMethod::kGet, static_cast<ChainHelper> (&App::get)),
  std::make_pair (HttpMethod::kHead, static_cast<ChainHelper> (&App::head)),
  std::make_pair (HttpMethod::kPost, static_cast<ChainHelper> (&App::post)),
  std::make_pair (HttpMethod::kPut, static_cast<ChainHelper> (&App::put)),
  std::make_pair (HttpMethod::kDelete, static_cast<ChainHelper> (&App::del)),
  std::make_pair (HttpMethod::kConnect, static_cast<ChainHelper> (&App::connect)),
  std::make_pair (HttpMethod::kOptions, static_cast<ChainHelper> (&App::options)),
  std::make_pair (HttpMethod::kTrace, static_cast<ChainHelper> (&App::trace)),
  std::make_pair (HttpMethod::kPatch, static_cast<ChainHelper> (&App::patch))));

TEST (Middleware, local_context_is_typed_and_isolated_between_requests) {
  App app;
  app.use ([] (auto &req, auto &, auto next) {
    EXPECT_TRUE (req.locals.empty());
    req.locals["user"] = std::string ("alice");
    req.locals["attempts"] = 2;
    next();
  });
  app.get ("/", [] (const auto &req, auto &res) {
    EXPECT_EQ (std::any_cast<int> (req.locals.at ("attempts")), 2);
    res.send (std::any_cast<std::string> (req.locals.at ("user")));
  });
  EXPECT_EQ (body (run (app)), "alice");
  EXPECT_EQ (body (run (app)), "alice");
}

TEST (Middleware, mutable_dispatch_preserves_locals_and_const_dispatch_uses_copy) {
  App app;
  app.use ([] (auto &req, auto &res, auto) { req.locals["value"] = 42; res.end(); });
  HttpRequest request { std::cref (logger) };
  ASSERT_TRUE (request.parse (get ("/")));
  HttpResponse first;
  app.dispatch (std::as_const (request), first);
  EXPECT_TRUE (request.locals.empty());
  HttpResponse second;
  app.dispatch (request, second);
  EXPECT_EQ (std::any_cast<int> (request.locals.at ("value")), 42);
  ASSERT_TRUE (request.parse (get ("/next")));
  EXPECT_TRUE (request.locals.empty());
}

TEST (Middleware, registration_during_dispatch_only_affects_subsequent_requests) {
  App app;
  bool registered = false;
  app.use ([&] (auto &, auto &, auto next) {
    if (!registered) {
      registered = true;
      app.get ("/", [] (const auto &, auto &res) { res.send ("new route"); });
      app.setDefault ([] (const auto &, auto &res) { res.send ("new fallback"); });
    }
    next();
  });
  EXPECT_EQ (run (app).status(), 404);
  EXPECT_EQ (body (run (app)), "new route");
  EXPECT_EQ (body (run (app, "/other")), "new fallback");
}

TEST (Middleware, callback_identity_survives_dispatch_and_registration) {
  App app;
  app.use ([count = 0] (auto &, auto &res, auto) mutable { res.send (std::to_string (++count)); });
  EXPECT_EQ (body (run (app)), "1");
  app.get ("/other", [] (const auto &, auto &res) { res.end(); });
  EXPECT_EQ (body (run (app)), "2");
}

TEST (Middleware, validates_callbacks_and_prefixes_before_registration) {
  App app;
  for (const auto prefix : { "", "api", "/api?q=1", "/api#part" })
    EXPECT_THROW (app.use (prefix, [] (auto &, auto &, auto next) { next(); }), std::invalid_argument);
  EXPECT_THROW (app.use (std::string_view ("/a\0b", 4), [] (auto &, auto &, auto next) { next(); }), std::invalid_argument);
  EXPECT_THROW (app.use (lightning::Middleware {}), std::invalid_argument);
  EXPECT_THROW (app.onError ({}), std::invalid_argument);
  EXPECT_THROW (app.get ("/", lightning::Middleware {}), std::invalid_argument);
  bool installed = false;
  EXPECT_THROW (app.get ("/", [&] (auto &, auto &, auto next) { installed = true; next(); },
    lightning::RequestHandler {}), std::invalid_argument);
  EXPECT_EQ (run (app).status(), 404);
  EXPECT_FALSE (installed);
  lightning::Dispatcher dispatcher;
  EXPECT_THROW (dispatcher.addRoute (HttpMethod::kGet, "/", std::vector<lightning::Middleware> {}), std::invalid_argument);
}

TEST (Middleware, incomplete_callback_is_an_error_instead_of_a_hanging_request) {
  App app;
  app.use ([] (auto &, auto &, auto) {});
  const auto response = run (app);
  EXPECT_EQ (response.status(), 500);
  EXPECT_TRUE (response.shouldClose());
}

TEST (Middleware, next_copies_share_single_use_state) {
  App app;
  int calls = 0;
  app.use ([] (auto &, auto &, auto next) {
    auto copy = next;
    next();
    EXPECT_THROW (copy(), std::logic_error);
  });
  app.get ("/", [&] (const auto &, auto &res) { ++calls; res.send ("once"); });
  EXPECT_EQ (body (run (app)), "once");
  EXPECT_EQ (calls, 1);
}

TEST (Middleware, unhandled_double_next_enters_error_chain_without_redispatch) {
  App app;
  int calls = 0;
  app.use ([] (auto &, auto &, auto next) { next(); next(); });
  app.get ("/", [&] (const auto &, auto &res) { ++calls; res.send ("partial"); });
  EXPECT_EQ (run (app).status(), 500);
  EXPECT_EQ (calls, 1);
}

TEST (Middleware, retained_next_expires_when_callback_returns_and_after_app_destruction) {
  Next saved;
  {
    App app;
    app.use ([&] (auto &, auto &res, auto next) { saved = next; res.end(); });
    EXPECT_EQ (run (app).status(), 200);
    EXPECT_THROW (saved(), std::logic_error);
  }
  EXPECT_THROW (saved(), std::logic_error);
  EXPECT_THROW (Next {}(), std::logic_error);
}

TEST (Middleware, cross_thread_next_is_rejected_without_consuming_owner_invocation) {
  App app;
  app.use ([] (auto &, auto &, auto next) {
    auto other = std::async (std::launch::async, [next] {
      EXPECT_THROW (next(), std::logic_error);
    });
    other.get();
    next();
  });
  app.get ("/", [] (const auto &, auto &res) { res.send ("owner"); });
  EXPECT_EQ (body (run (app)), "owner");
}

TEST (Middleware, next_after_send_cannot_execute_later_routes) {
  App app;
  app.use ([] (auto &, auto &res, auto next) { res.send ("done"); next(); });
  app.get ("/", [] (const auto &, auto &) { FAIL() << "Already finished"; });
  EXPECT_EQ (run (app).status(), 500);
}

TEST (Middleware, double_send_and_status_after_completion_are_rejected) {
  HttpResponse response;
  response.status (201).send ("first");
  EXPECT_THROW (response.send ("second"), std::logic_error);
  EXPECT_THROW (response.status (400), std::logic_error);
  EXPECT_THROW (response.end(), std::logic_error);
  EXPECT_EQ (response.status(), 201);
  EXPECT_EQ (body (response), "first");
  App app;
  HttpRequest request { std::cref (logger) };
  EXPECT_THROW (app.dispatch (request, response), std::logic_error);
}

TEST (MiddlewareErrors, thrown_and_forwarded_errors_share_the_ordered_error_chain) {
  for (bool forward : { false, true }) {
    App app;
    std::vector<int> order;
    EXPECT_EQ (&app.onError ([&] (auto error, auto &req, auto &, auto next) {
      EXPECT_EQ (std::any_cast<int> (req.locals.at ("context")), 7);
      EXPECT_THROW (std::rethrow_exception (error), std::runtime_error);
      order.push_back (1);
      next();
    }), &app);
    app.onError ([&] (auto, auto &, auto &res, auto) {
      order.push_back (2);
      res.status (422).send ("handled");
    });
    app.use ([forward] (auto &req, auto &, auto next) {
      req.locals["context"] = 7;
      if (forward) next (std::make_exception_ptr (std::runtime_error ("private")));
      else throw std::runtime_error ("private");
    });
    app.get ("/", [] (const auto &, auto &) { FAIL() << "Normal routes must be skipped"; });
    const auto response = run (app);
    EXPECT_EQ (response.status(), 422);
    EXPECT_EQ (body (response), "handled");
    EXPECT_FALSE (response.shouldClose());
    EXPECT_EQ (order, (std::vector<int> { 1, 2 }));
  }
}

TEST (MiddlewareErrors, route_and_default_exceptions_discard_partial_responses) {
  App app;
  const lightning::RequestHandler fail = [] (const auto &, auto &res) {
    res.headers().set ("x-partial", "discard");
    res.status (201).send ("private body");
    throw 42;
  };
  app.get ("/throw", fail);
  app.setDefault (fail);
  app.onError ([] (auto error, auto &, auto &res, auto) {
    EXPECT_THROW (std::rethrow_exception (error), int);
    EXPECT_FALSE (res.finished());
    EXPECT_FALSE (res.headers().contains ("x-partial"));
    res.status (503).send ("retry");
  });
  for (const auto path : { "/throw", "/missing" }) {
    const auto response = run (app, path);
    EXPECT_EQ (response.status(), 503);
    EXPECT_EQ (body (response), "retry");
  }
}

TEST (MiddlewareErrors, forwarded_replacement_errors_reach_the_next_error_handler) {
  App app;
  app.use ([] (auto &, auto &, auto) { throw 42; });
  app.onError ([] (auto, auto &, auto &, auto next) {
    next (std::make_exception_ptr (std::invalid_argument ("replacement")));
  });
  app.onError ([] (auto error, auto &, auto &res, auto) {
    EXPECT_THROW (std::rethrow_exception (error), std::invalid_argument);
    res.status (400).end();
  });
  EXPECT_EQ (run (app).status(), 400);
}

TEST (MiddlewareErrors, throwing_error_handler_advances_without_recursion_or_partial_output) {
  App app;
  int calls = 0;
  app.use ([] (auto &, auto &, auto) { throw 42; });
  app.onError ([&] (auto, auto &, auto &res, auto) {
    ++calls;
    res.send ("partial");
    throw std::runtime_error ("error handler failed");
  });
  app.onError ([] (auto error, auto &, auto &res, auto) {
    EXPECT_THROW (std::rethrow_exception (error), std::runtime_error);
    EXPECT_FALSE (res.finished());
    res.status (502).send ("recovered");
  });
  EXPECT_EQ (body (run (app)), "recovered");
  EXPECT_EQ (calls, 1);
}

TEST (MiddlewareErrors, exception_after_next_never_reenters_an_error_handler) {
  App app;
  int calls = 0;
  app.use ([] (auto &, auto &, auto) { throw 42; });
  for (int i = 0; i < 10; ++i) {
    app.onError ([&] (auto, auto &, auto &, auto next) {
      ++calls;
      next();
      throw std::runtime_error ("failed while unwinding");
    });
  }
  EXPECT_EQ (run (app).status(), 500);
  EXPECT_EQ (calls, 10);
}

TEST (MiddlewareErrors, next_expires_before_the_error_handler_runs) {
  App app;
  Next saved;
  app.use ([&] (auto &, auto &, auto next) { saved = next; throw 42; });
  app.onError ([&] (auto, auto &, auto &res, auto) {
    EXPECT_THROW (saved(), std::logic_error);
    res.status (400).end();
  });
  EXPECT_EQ (run (app).status(), 400);
}

TEST (MiddlewareErrors, incomplete_or_exhausted_error_chain_produces_generic_500) {
  for (bool forwards : { false, true }) {
    App app;
    app.use ([] (auto &, auto &, auto) { throw std::runtime_error ("private detail"); });
    app.onError ([forwards] (auto error, auto &, auto &, auto next) { if (forwards) next (error); });
    const auto response = run (app);
    EXPECT_EQ (response.status(), 500);
    EXPECT_EQ (body (response), "Internal server error");
    EXPECT_TRUE (response.shouldClose());
  }
}

TEST (MiddlewareErrors, error_continuations_expire_and_cannot_send_twice) {
  App app;
  Next saved;
  app.use ([] (auto &, auto &, auto) { throw 42; });
  app.onError ([&] (auto, auto &, auto &res, auto next) {
    saved = next;
    res.status (400).send ("first");
    next();
  });
  const auto response = run (app);
  EXPECT_EQ (response.status(), 500);
  EXPECT_THROW (saved(), std::logic_error);
}

TEST (MiddlewareErrors, normal_404_does_not_enter_error_chain) {
  App app;
  app.use ([] (auto &, auto &, auto next) { next(); });
  app.onError ([] (auto, auto &, auto &, auto) { FAIL() << "404 is not an exception"; });
  EXPECT_EQ (run (app).status(), 404);
}

TEST (MiddlewareErrors, registration_from_error_handler_applies_to_next_request_only) {
  App app;
  bool registered = false;
  app.use ([] (auto &, auto &, auto) { throw 42; });
  app.onError ([&] (auto error, auto &, auto &, auto next) {
    if (!registered) {
      registered = true;
      app.onError ([] (auto, auto &, auto &res, auto) { res.status (418).end(); });
    }
    next (error);
  });
  EXPECT_EQ (run (app).status(), 500);
  EXPECT_EQ (run (app).status(), 418);
}

TEST (Middleware, concurrent_dispatch_and_configuration_keep_context_separate) {
  App app;
  app.use ([] (auto &req, auto &, auto next) { req.locals["path"] = req.path; next(); });
  app.setDefault ([] (const auto &req, auto &res) { res.send (std::any_cast<std::string> (req.locals.at ("path"))); });
  std::atomic<int> failures { 0 };
  std::vector<std::thread> threads;
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back ([&, i] {
      const std::string path = "/" + std::to_string (i);
      for (int j = 0; j < 100; ++j)
        if (body (run (app, path)) != path) ++failures;
    });
  }
  for (int i = 0; i < 50; ++i) {
    app.use ([] (auto &, auto &, auto next) { next(); });
    app.onError ([] (auto error, auto &, auto &, auto next) { next (error); });
  }
  for (auto &thread : threads) thread.join();
  EXPECT_EQ (failures, 0);
}

TEST (MiddlewareSockets, authentication_and_handled_errors_preserve_pipelining_and_context_isolation) {
  App app;
  app.use ("/private", [] (auto &req, auto &res, auto next) {
    if (!req.headers.contains ("authorization")) { res.status (401).send ("denied"); return; }
    req.locals["user"] = std::string ("alice");
    next();
  });
  app.get ("/private", [] (const auto &req, auto &res) { res.send (std::any_cast<std::string> (req.locals.at ("user"))); });
  app.get ("/throw", [] (const auto &, auto &) { throw 42; });
  app.get ("/public", [] (const auto &req, auto &res) { EXPECT_TRUE (req.locals.empty()); res.send ("public"); });
  app.onError ([] (auto, auto &, auto &res, auto) { res.status (422).send ("handled"); });
  app.start (localOptions());
  Client client { app.port() };
  client.send (get ("/private") + "GET /private HTTP/1.1\r\nAuthorization: yes\r\n\r\n" + get ("/throw") + get ("/public"));
  EXPECT_EQ (client.read().status, 401);
  EXPECT_EQ (client.read().body, "alice");
  EXPECT_EQ (client.read().status, 422);
  EXPECT_EQ (client.read().body, "public");
}

TEST (MiddlewareSockets, unhandled_error_closes_connection_and_discards_pipelined_request) {
  App app;
  std::atomic<int> calls { 0 };
  app.use ([] (auto &, auto &, auto) { throw std::runtime_error ("private"); });
  app.get ("/", [&] (const auto &, auto &res) { ++calls; res.send ("unexpected"); });
  app.start (localOptions());
  Client client { app.port() };
  client.send (get ("/") + get ("/"));
  const auto response = client.read();
  EXPECT_EQ (response.status, 500);
  EXPECT_EQ (response.body, "Internal server error");
  EXPECT_EQ (response.headers.at ("connection"), "close");
  EXPECT_TRUE (client.waitForClose());
  EXPECT_EQ (calls, 0);
}

TEST (MiddlewareSockets, handled_head_errors_preserve_framing_of_following_response) {
  App app;
  app.head ("/", [] (const auto &, auto &) { throw 42; });
  app.get ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  app.onError ([] (auto, auto &, auto &res, auto) { res.status (422).send ("handled"); });
  app.start (localOptions());
  Client client { app.port() };
  client.send ("HEAD / HTTP/1.1\r\n\r\n" + get ("/"));
  const auto head = client.read (true);
  EXPECT_EQ (head.status, 422);
  EXPECT_EQ (head.headers.at ("content-length"), "7");
  EXPECT_EQ (client.read().body, "ok");
}

TEST (MiddlewareSockets, parser_errors_do_not_invoke_application_middleware_or_error_handlers) {
  App app;
  std::atomic<int> calls { 0 };
  app.use ([&] (auto &, auto &, auto next) { ++calls; next(); });
  app.onError ([&] (auto, auto &, auto &res, auto) { ++calls; res.status (422).end(); });
  app.start (localOptions());
  Client client { app.port() };
  client.send ("GET / HTTP/1.1\r\nBad Header: invalid\r\n\r\n");
  EXPECT_EQ (client.read().status, 400);
  EXPECT_TRUE (client.waitForClose());
  EXPECT_EQ (calls, 0);
}

TEST (MiddlewareSockets, method_rewrites_preserve_original_head_wire_framing) {
  App app;
  app.use ([] (auto &req, auto &, auto next) {
    if (req.method == HttpMethod::kHead) req.method = HttpMethod::kGet;
    next();
  });
  app.get ("/", [] (const auto &, auto &res) { res.send ("ok"); });
  app.get ("/throw", [] (const auto &, auto &) { throw 42; });
  app.onError ([] (auto, auto &, auto &res, auto) { res.status (422).send ("handled"); });
  app.start (localOptions());
  Client client { app.port() };
  client.send ("HEAD / HTTP/1.1\r\n\r\n" + get ("/") +
    "HEAD /throw HTTP/1.1\r\n\r\n" + get ("/"));
  EXPECT_EQ (client.read (true).headers.at ("content-length"), "2");
  EXPECT_EQ (client.read().body, "ok");
  const auto error = client.read (true);
  EXPECT_EQ (error.status, 422);
  EXPECT_EQ (error.headers.at ("content-length"), "7");
  EXPECT_EQ (client.read().body, "ok");
}

}
