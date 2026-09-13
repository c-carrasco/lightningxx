// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <lightning/app.h>
#include "http_test_client.h"

namespace {
using lightning::App;
using lightning::Router;
using lightning::HttpMethod;
using lightning::HttpRequest;
using lightning::HttpResponse;
using lightning::RouteDecodeError;
using lightning::test::Client;
lightning::Logger logger { lightning::LogLevel::kFatal };

std::string body (const HttpResponse &response) {
  const auto wire = response.data();
  return wire.substr (wire.find ("\r\n\r\n") + 4);
}

template<class Routes>
HttpResponse run (const Routes &routes, std::string_view path, HttpMethod method = HttpMethod::kGet) {
  HttpRequest request { std::cref (logger) };
  request.method = method;
  request.path = path;
  request.url = path;
  HttpResponse response;
  routes.dispatch (request, response);
  return response;
}

std::string get (std::string_view path) {
  return "GET " + std::string (path) + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

lightning::ServerOptions localOptions() {
  lightning::ServerOptions options;
  options.port = 0;
  options.workers = 3;
  options.logLevel = lightning::LogLevel::kFatal;
  return options;
}

TEST (RouteParameters, captures_multiple_named_segments_and_excludes_query) {
  App app;
  app.get ("/users/:userId/books/:bookId", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.params.size(), 2);
    EXPECT_EQ (req.query, "sort=asc");
    res.send (req.params.at ("userId") + ":" + req.params.at ("bookId"));
  });
  HttpRequest request { std::cref (logger) };
  ASSERT_TRUE (request.parse (get ("/users/42/books/7?sort=asc")));
  HttpResponse response;
  app.dispatch (request, response);
  EXPECT_EQ (body (response), "42:7");
  EXPECT_TRUE (request.params.empty());
  EXPECT_EQ (request.path, "/users/42/books/7");
}

TEST (RouteParameters, matches_exact_segments_case_and_trailing_slashes) {
  App app;
  app.get ("/users/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  EXPECT_EQ (body (run (app, "/users/42")), "42");
  for (const auto path : { "/users", "/users/", "/users//", "/users/42/", "/users/42/books", "/Users/42" })
    EXPECT_EQ (run (app, path).status(), 404) << path;
  app.get ("/users/:id/", [] (const auto &, auto &res) { res.send ("trailing"); });
  EXPECT_EQ (body (run (app, "/users/42/")), "trailing");
}

TEST (RouteParameters, wildcard_captures_nonempty_remainder_as_one_owned_string) {
  App app;
  app.get ("/files/*path", [] (const auto &req, auto &res) { res.send (req.params.at ("path")); });
  EXPECT_EQ (body (run (app, "/files/a")), "a");
  EXPECT_EQ (body (run (app, "/files/a/b.txt")), "a/b.txt");
  EXPECT_EQ (body (run (app, "/files/a/")), "a/");
  EXPECT_EQ (run (app, "/files").status(), 404);
  EXPECT_EQ (run (app, "/files/").status(), 404);
}

TEST (RouteParameters, percent_decodes_once_without_form_decoding_or_changing_segments) {
  App app;
  app.get ("/values/:value", [] (const auto &req, auto &res) { res.send (req.params.at ("value")); });
  for (const auto &[encoded, decoded] : std::vector<std::pair<std::string, std::string>> {
      { "hello%20world", "hello world" }, { "a+b", "a+b" }, { "a%2Fb", "a/b" },
      { "%252F", "%2F" }, { "%C3%A9", "\xc3\xa9" }, { "%3f%23", "?#" } }) {
    EXPECT_EQ (body (run (app, "/values/" + encoded)), decoded);
  }
  EXPECT_EQ (run (app, "/values/a/b").status(), 404);
  app.get ("/files/*path", [] (const auto &req, auto &res) { res.send (req.params.at ("path")); });
  EXPECT_EQ (body (run (app, "/files/a%20b/c%252Fd")), "a b/c%2Fd");
}

TEST (RouteParameters, invalid_captures_default_to_400_and_can_be_customized) {
  App app;
  int calls = 0;
  app.get ("/values/:value", [&] (const auto &, auto &res) { ++calls; res.end(); });
  for (const auto value : { "%", "%2", "%GG", "%00" }) {
    const auto response = run (app, std::string ("/values/") + value);
    EXPECT_EQ (response.status(), 400);
    EXPECT_EQ (body (response), "Bad request");
    EXPECT_TRUE (response.shouldClose());
  }
  EXPECT_EQ (calls, 0);
  app.onError ([] (auto error, auto &, auto &res, auto) {
    EXPECT_THROW (std::rethrow_exception (error), RouteDecodeError);
    res.status (422).send ("bad parameter");
  });
  EXPECT_EQ (run (app, "/values/%ZZ").status(), 422);
}

TEST (RouteParameters, nonmatching_routes_do_not_decode_partial_captures) {
  App app;
  app.get ("/values/:value/books", [] (const auto &, auto &) { FAIL(); });
  app.post ("/values/:value", [] (const auto &, auto &) { FAIL(); });
  EXPECT_EQ (run (app, "/values/%GG/other").status(), 404);
  EXPECT_EQ (run (app, "/values/%GG").status(), 404);
}

TEST (RouteParameters, literal_segments_remain_raw_and_reserved_characters_can_be_encoded) {
  App app;
  app.get ("/literal%20path/%3Aname", [] (const auto &, auto &res) { res.send ("literal"); });
  EXPECT_EQ (body (run (app, "/literal%20path/%3Aname")), "literal");
  EXPECT_EQ (run (app, "/literal path/:name").status(), 404);
  app.options ("*", [] (const auto &, auto &res) { res.send ("asterisk"); });
  EXPECT_EQ (body (run (app, "*", HttpMethod::kOptions)), "asterisk");
  EXPECT_EQ (run (app, "/anything", HttpMethod::kOptions).status(), 404);
}

TEST (RouteParameters, precedence_remains_registration_order_without_literal_priority) {
  App app;
  app.get ("/users/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  app.get ("/users/me", [] (const auto &, auto &res) { res.send ("literal"); });
  EXPECT_EQ (body (run (app, "/users/me")), "me");
  App reversed;
  reversed.get ("/users/me", [] (const auto &, auto &res) { res.send ("literal"); });
  reversed.get ("/users/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  EXPECT_EQ (body (run (reversed, "/users/me")), "literal");
}

TEST (RouteParameters, parameters_are_scoped_across_fallthrough_and_unwinding) {
  App app;
  app.get ("/users/:id", [] (auto &req, auto &, auto next) {
    EXPECT_EQ (req.params.at ("id"), "me");
    next();
    EXPECT_EQ (req.params.at ("id"), "me");
  });
  app.use ([] (auto &req, auto &, auto next) { EXPECT_TRUE (req.params.empty()); next(); });
  app.get ("/users/:name", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.params.size(), 1);
    res.send (req.params.at ("name"));
  });
  EXPECT_EQ (body (run (app, "/users/me")), "me");
}

TEST (RouteParameters, error_handlers_see_the_failing_routes_parameters) {
  App app;
  app.get ("/users/:id", [] (const auto &, auto &) { throw 42; });
  app.onError ([] (auto, auto &req, auto &res, auto) { res.status (422).send (req.params.at ("id")); });
  EXPECT_EQ (body (run (app, "/users/alice")), "alice");
}

TEST (RouteParameters, invalid_patterns_fail_before_changing_configuration) {
  App app;
  for (const auto pattern : { "", "users/:id", "/users/:", "/:1id", "/:id-name", "/file-:id", "/:id/:id",
      "/files/*", "/files/*path/more", "/files/*path/", "/:id?", "/{id}", "/[id]", "/(id)", "/x?q=1" }) {
    EXPECT_THROW (app.get (pattern, [] (const auto &, auto &res) { res.end(); }), std::invalid_argument) << pattern;
  }
  EXPECT_THROW (app.get (std::string_view ("/x\0y", 4), [] (const auto &, auto &res) { res.end(); }), std::invalid_argument);
  EXPECT_EQ (run (app, "/users/42").status(), 404);
}

TEST (RouteParameters, registration_owns_pattern_and_captured_data_can_outlive_dispatch) {
  App app;
  std::optional<HttpRequest> captured;
  std::string pattern = "/" + std::string (200, 'x') + "/:id";
  const auto prefix = pattern.substr (0, pattern.size() - 4);
  app.get (pattern, [&] (const auto &req, auto &res) { captured = req; res.end(); });
  pattern.assign (pattern.size(), 'z');
  EXPECT_EQ (run (app, prefix + "/owned%20value").status(), 200);
  ASSERT_TRUE (captured);
  EXPECT_EQ (captured->params.at ("id"), "owned value");
}

TEST (Router, mounts_relative_paths_and_preserves_original_url_and_query) {
  App app;
  Router users;
  users.get ("/:id", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.path, "/42");
    EXPECT_EQ (req.baseUrl, "/api/users");
    EXPECT_EQ (req.url, "/api/users/42?sort=asc");
    EXPECT_EQ (req.query, "sort=asc");
    res.send (req.params.at ("id"));
  });
  EXPECT_EQ (&app.use ("/api/users", users), &app);
  HttpRequest request { std::cref (logger) };
  ASSERT_TRUE (request.parse (get ("/api/users/42?sort=asc")));
  HttpResponse response;
  app.dispatch (request, response);
  EXPECT_EQ (body (response), "42");
  EXPECT_EQ (request.path, "/api/users/42");
  EXPECT_TRUE (request.baseUrl.empty());
  EXPECT_TRUE (request.params.empty());
}

TEST (Router, mount_root_trailing_slashes_and_boundaries_are_explicit) {
  App app;
  Router api;
  api.get ("/", [] (const auto &, auto &res) { res.send ("root"); });
  app.use ("/api/", api);
  EXPECT_EQ (body (run (app, "/api")), "root");
  EXPECT_EQ (body (run (app, "/api/")), "root");
  EXPECT_EQ (run (app, "/apiculture").status(), 404);
  EXPECT_EQ (run (app, "/API").status(), 404);
  App root;
  root.use (api);
  EXPECT_EQ (body (run (root, "/")), "root");
  EXPECT_EQ (run (root, "/other").status(), 404);
}

TEST (Router, nested_mounts_merge_parameters_and_restore_shadowed_names) {
  App app;
  Router organizations;
  Router users;
  organizations.use ([] (auto &req, auto &, auto next) {
    EXPECT_EQ (req.params.at ("id"), "organization");
    EXPECT_EQ (req.baseUrl, "/orgs/organization");
    next();
    EXPECT_EQ (req.params.at ("id"), "organization");
  });
  users.get ("/:id", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.baseUrl, "/orgs/organization/users");
    EXPECT_EQ (req.params.at ("id"), "alice");
    res.send (req.path);
  });
  EXPECT_EQ (&organizations.use ("/users", users), &organizations);
  app.use ("/orgs/:id", organizations);
  EXPECT_EQ (body (run (app, "/orgs/organization/users/alice")), "/alice");
}

TEST (Router, middleware_can_capture_prefix_params_without_stripping_path) {
  App app;
  app.use ("/orgs/:org", [] (auto &req, auto &, auto next) {
    EXPECT_EQ (req.params.at ("org"), "a b");
    EXPECT_EQ (req.path, "/orgs/a%20b/users");
    EXPECT_TRUE (req.baseUrl.empty());
    req.locals["org"] = req.params.at ("org");
    next();
  });
  app.get ("/orgs/:id/users", [] (const auto &req, auto &res) {
    EXPECT_FALSE (req.params.contains ("org"));
    res.send (std::any_cast<std::string> (req.locals.at ("org")));
  });
  EXPECT_EQ (body (run (app, "/orgs/a%20b/users")), "a b");
}

TEST (Router, encoded_mount_values_do_not_change_boundaries_or_decode_twice) {
  App app;
  Router child;
  child.get ("/:id", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.params.at ("org"), "a/b");
    EXPECT_EQ (req.params.at ("id"), "%2F");
    EXPECT_EQ (req.baseUrl, "/orgs/a%2Fb");
    EXPECT_EQ (req.path, "/%252F");
    res.end();
  });
  app.use ("/orgs/:org", child);
  EXPECT_EQ (run (app, "/orgs/a%2Fb/%252F").status(), 200);
  EXPECT_EQ (run (app, "/orgs/a/b/%252F").status(), 404);
}

TEST (Router, local_path_rewrites_are_restored_before_parent_fallthrough) {
  App app;
  Router child;
  child.use ([] (auto &req, auto &, auto next) { req.path = "/rewritten"; next(); });
  child.get ("/rewritten", [] (auto &, auto &, auto next) { next(); });
  app.use ("/api", child);
  app.setDefault ([] (const auto &req, auto &res) {
    EXPECT_TRUE (req.baseUrl.empty());
    res.send (req.path);
  });
  EXPECT_EQ (body (run (app, "/api/original")), "/api/original");
}

TEST (RouteParameters, parsing_and_dispatch_clear_stale_routing_context) {
  App app;
  HttpRequest request { std::cref (logger) };
  request.params["stale"] = "value";
  request.baseUrl = "/old";
  HttpResponse invalid;
  app.dispatch (request, invalid);
  EXPECT_TRUE (request.params.empty());
  EXPECT_TRUE (request.baseUrl.empty());
  request.params["stale"] = "value";
  request.baseUrl = "/old";
  ASSERT_TRUE (request.parse (get ("/new")));
  EXPECT_TRUE (request.params.empty());
  EXPECT_TRUE (request.baseUrl.empty());
}

TEST (Router, fallthrough_restores_parent_context_and_child_unwinding_context) {
  App app;
  Router api;
  api.use ([] (auto &req, auto &, auto next) {
    EXPECT_EQ (req.path, "/missing");
    EXPECT_EQ (req.baseUrl, "/api");
    req.locals["visited"] = true;
    next();
    EXPECT_EQ (req.path, "/missing");
    EXPECT_EQ (req.baseUrl, "/api");
  });
  app.use ("/api", api);
  app.get ("/api/missing", [] (const auto &req, auto &res) {
    EXPECT_TRUE (req.baseUrl.empty());
    EXPECT_TRUE (req.params.empty());
    EXPECT_TRUE (std::any_cast<bool> (req.locals.at ("visited")));
    res.send (req.path);
  });
  EXPECT_EQ (body (run (app, "/api/missing")), "/api/missing");
}

TEST (Router, route_next_can_leave_router_and_parent_registration_order_is_preserved) {
  App app;
  Router first;
  Router second;
  first.get ("/:id", [] (auto &req, auto &, auto next) {
    EXPECT_EQ (req.params.at ("id"), "42"); next();
    EXPECT_EQ (req.params.at ("id"), "42");
  });
  second.get ("/:name", [] (const auto &req, auto &res) {
    EXPECT_FALSE (req.params.contains ("id"));
    res.send (req.params.at ("name"));
  });
  app.use ("/api", first);
  app.use ("/api", second);
  app.get ("/api/42", [] (const auto &, auto &) { FAIL(); });
  EXPECT_EQ (body (run (app, "/api/42")), "42");
}

TEST (Router, local_default_is_terminal_and_can_be_reset_to_parent_fallthrough) {
  App app;
  Router router;
  router.setDefault ([] (const auto &req, auto &res) { res.status (410).send (req.path); });
  app.use ("/api", router);
  app.setDefault ([] (const auto &, auto &res) { res.status (404).send ("parent"); });
  EXPECT_EQ (body (run (app, "/api/missing")), "/missing");
  router.setDefault ({});
  EXPECT_EQ (body (run (app, "/api/missing")), "parent");
}

TEST (Router, shared_live_configuration_survives_handle_destruction_and_multiple_mounts) {
  App app;
  {
    Router router;
    auto alias = router;
    app.use ("/v1", router);
    app.use ("/v2", router);
    alias.get ("/:id", [] (const auto &req, auto &res) { res.send (req.baseUrl + ":" + req.params.at ("id")); });
  }
  EXPECT_EQ (body (run (app, "/v1/a")), "/v1:a");
  EXPECT_EQ (body (run (app, "/v2/b")), "/v2:b");
}

TEST (Router, complete_graph_is_snapshotted_before_any_callback_runs) {
  App app;
  Router router;
  bool added = false;
  app.use ([&] (auto &, auto &, auto next) {
    if (!added) {
      added = true;
      router.get ("/new", [] (const auto &, auto &res) { res.send ("new"); });
    }
    next();
  });
  app.use ("/api", router);
  EXPECT_EQ (run (app, "/api/new").status(), 404);
  EXPECT_EQ (body (run (app, "/api/new")), "new");
}

TEST (Router, shared_router_uses_one_snapshot_for_every_mount_in_a_request) {
  App app;
  Router router;
  bool added = false;
  router.use ([&] (auto &, auto &, auto next) {
    if (!added) {
      added = true;
      router.get ("/new", [] (const auto &, auto &res) { res.send ("new"); });
    }
    next();
  });
  app.use ("/api", router);
  app.use ("/api", router);
  EXPECT_EQ (run (app, "/api/new").status(), 404);
  EXPECT_EQ (body (run (app, "/api/new")), "new");
}

TEST (Router, rejects_self_and_indirect_cycles_without_rejecting_shared_children) {
  Router parent;
  Router child;
  Router grandchild;
  EXPECT_THROW (parent.use (parent), std::invalid_argument);
  auto alias = parent;
  EXPECT_THROW (parent.use (alias), std::invalid_argument);
  parent.use (child);
  child.use (grandchild);
  EXPECT_THROW (grandchild.use (parent), std::invalid_argument);
  EXPECT_NO_THROW (parent.use ("/again", grandchild));
  EXPECT_EQ (run (parent, "/missing").status(), 404);
}

TEST (Router, concurrent_opposite_mounts_cannot_create_a_cycle) {
  Router first;
  Router second;
  std::atomic<int> accepted { 0 };
  std::atomic<int> rejected { 0 };
  auto mount = [&] (Router &parent, const Router &child) {
    try { parent.use (child); ++accepted; }
    catch (const std::invalid_argument &) { ++rejected; }
  };
  std::thread a { [&] { mount (first, second); } };
  std::thread b { [&] { mount (second, first); } };
  a.join(); b.join();
  EXPECT_EQ (accepted, 1);
  EXPECT_EQ (rejected, 1);
}

TEST (Router, mount_patterns_validate_before_installation_and_own_their_prefix) {
  App app;
  Router router;
  router.get ("/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  for (const auto prefix : { "", "api", "/api/*rest", "/:id/:id" })
    EXPECT_THROW (app.use (prefix, router), std::invalid_argument);
  std::string prefix = "/api";
  app.use (prefix, router);
  prefix = "/changed";
  EXPECT_EQ (body (run (app, "/api/a")), "a");
  EXPECT_EQ (run (app, "/changed/a").status(), 404);
}

TEST (RouterErrors, local_errors_see_relative_path_and_route_params_then_propagate_to_parent) {
  App app;
  Router router;
  std::vector<int> order;
  router.get ("/:id", [] (const auto &, auto &) { throw 42; });
  router.onError ([&] (auto error, auto &req, auto &, auto next) {
    order.push_back (1);
    EXPECT_EQ (req.path, "/alice");
    EXPECT_EQ (req.params.at ("id"), "alice");
    EXPECT_EQ (req.baseUrl, "/api");
    next (error);
    EXPECT_EQ (req.path, "/alice");
    EXPECT_EQ (req.params.at ("id"), "alice");
  });
  app.use ("/api", router);
  app.onError ([&] (auto error, auto &req, auto &res, auto) {
    order.push_back (2);
    EXPECT_THROW (std::rethrow_exception (error), int);
    EXPECT_EQ (req.path, "/api/alice");
    EXPECT_TRUE (req.params.empty());
    EXPECT_TRUE (req.baseUrl.empty());
    res.status (422).send ("handled");
  });
  EXPECT_EQ (run (app, "/api/alice").status(), 422);
  EXPECT_EQ (order, (std::vector<int> { 1, 2 }));
}

TEST (RouterErrors, handled_child_error_does_not_invoke_parent_error_handlers) {
  App app;
  Router router;
  router.setDefault ([] (const auto &, auto &) { throw 42; });
  router.onError ([] (auto, auto &, auto &res, auto) { res.status (409).end(); });
  app.use (router);
  app.onError ([] (auto, auto &, auto &, auto) { FAIL(); });
  EXPECT_EQ (run (app, "/missing").status(), 409);
}

TEST (RouterErrors, malformed_mount_parameter_enters_parent_error_chain) {
  App app;
  Router router;
  router.onError ([] (auto, auto &, auto &, auto) { FAIL() << "Router was never entered"; });
  app.use ("/api/:id", router);
  const auto response = run (app, "/api/%GG/items");
  EXPECT_EQ (response.status(), 400);
  EXPECT_TRUE (response.shouldClose());
}

TEST (RouterErrors, child_decode_failure_propagates_400_and_can_be_handled_locally) {
  App app;
  Router router;
  router.get ("/:id", [] (const auto &, auto &) { FAIL(); });
  app.use ("/api", router);
  EXPECT_EQ (run (app, "/api/%GG").status(), 400);
  router.onError ([] (auto error, auto &, auto &res, auto) {
    EXPECT_THROW (std::rethrow_exception (error), RouteDecodeError);
    res.status (422).end();
  });
  EXPECT_EQ (run (app, "/api/%GG").status(), 422);
}

TEST (Router, concurrent_requests_and_registration_keep_route_context_separate) {
  App app;
  Router router;
  router.get ("/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("org") + ":" + req.params.at ("id")); });
  app.use ("/orgs/:org", router);
  std::atomic<int> failures { 0 };
  std::vector<std::thread> threads;
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back ([&, i] {
      const auto value = std::to_string (i);
      for (int j = 0; j < 100; ++j)
        if (body (run (app, "/orgs/" + value + "/user")) != value + ":user") ++failures;
    });
  }
  for (int i = 0; i < 100; ++i)
    router.use ([] (auto &, auto &, auto next) { next(); });
  for (auto &thread : threads) thread.join();
  EXPECT_EQ (failures, 0);
}

TEST (Router, graph_snapshots_allow_concurrent_mount_registration) {
  App app;
  Router router;
  app.use ("/api", router);
  app.setDefault ([] (const auto &, auto &res) { res.send ("fallback"); });
  std::atomic<int> failures { 0 };
  auto reader = std::async (std::launch::async, [&] {
    for (int i = 0; i < 100; ++i)
      if (body (run (app, "/api/missing")) != "fallback") ++failures;
  });
  for (int i = 0; i < 50; ++i) {
    Router child;
    child.get ("/", [] (const auto &, auto &res) { res.send ("child"); });
    router.use ("/child" + std::to_string (i), child);
  }
  reader.get();
  EXPECT_EQ (failures, 0);
  EXPECT_EQ (body (run (app, "/api/child49")), "child");
}

using MethodHelper = Router & (Router::*) (std::string_view, lightning::Middleware, lightning::RequestHandler);
class RouterMethods: public ::testing::TestWithParam<std::pair<HttpMethod, MethodHelper>> {};
TEST_P (RouterMethods, every_method_supports_parameters_and_middleware) {
  Router router;
  const auto [method, helper] = GetParam();
  (router.*helper) ("/:id", [] (auto &req, auto &, auto next) { EXPECT_EQ (req.params.at ("id"), "42"); next(); },
    [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  for (int i = 0; i < lightning::kNumHttpMethods; ++i) {
    const auto candidate = static_cast<HttpMethod> (i);
    const bool matched = candidate == method || (method == HttpMethod::kGet && candidate == HttpMethod::kHead);
    EXPECT_EQ (run (router, "/42", candidate).status(), matched ? 200 : candidate == HttpMethod::kOptions ? 204 : 404);
  }
}
INSTANTIATE_TEST_SUITE_P (AllMethods, RouterMethods, ::testing::Values (
  std::make_pair (HttpMethod::kGet, static_cast<MethodHelper> (&Router::get)),
  std::make_pair (HttpMethod::kHead, static_cast<MethodHelper> (&Router::head)),
  std::make_pair (HttpMethod::kPost, static_cast<MethodHelper> (&Router::post)),
  std::make_pair (HttpMethod::kPut, static_cast<MethodHelper> (&Router::put)),
  std::make_pair (HttpMethod::kDelete, static_cast<MethodHelper> (&Router::del)),
  std::make_pair (HttpMethod::kConnect, static_cast<MethodHelper> (&Router::connect)),
  std::make_pair (HttpMethod::kOptions, static_cast<MethodHelper> (&Router::options)),
  std::make_pair (HttpMethod::kTrace, static_cast<MethodHelper> (&Router::trace)),
  std::make_pair (HttpMethod::kPatch, static_cast<MethodHelper> (&Router::patch))));

TEST (RouterSockets, nested_routes_decode_params_and_keep_pipelined_requests_isolated) {
  App app;
  Router api;
  Router users;
  users.get ("/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  users.head ("/:id", [] (const auto &req, auto &res) { res.send (req.params.at ("id")); });
  api.use ("/users", users);
  app.use ("/api", api);
  app.get ("/public", [] (const auto &req, auto &res) { EXPECT_TRUE (req.params.empty()); EXPECT_TRUE (req.baseUrl.empty()); res.send ("public"); });
  app.start (localOptions());
  Client client { app.port() };
  client.send (get ("/api/users/alice%20smith") + "HEAD /api/users/bob HTTP/1.1\r\n\r\n" + get ("/public"));
  EXPECT_EQ (client.read().body, "alice smith");
  EXPECT_EQ (client.read (true).headers.at ("content-length"), "3");
  EXPECT_EQ (client.read().body, "public");
}

TEST (RouterSockets, unhandled_decode_errors_close_connection_without_serving_following_request) {
  App app;
  Router router;
  std::atomic<int> calls { 0 };
  router.get ("/:id", [&] (const auto &, auto &res) { ++calls; res.end(); });
  app.use ("/api", router);
  app.start (localOptions());
  Client client { app.port() };
  client.send (get ("/api/%GG") + get ("/api/valid"));
  const auto response = client.read();
  EXPECT_EQ (response.status, 400);
  EXPECT_EQ (response.body, "Bad request");
  EXPECT_TRUE (client.waitForClose());
  EXPECT_EQ (calls, 0);
}

}
