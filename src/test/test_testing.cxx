// MIT License - Copyright (c) 2026 Carlos Carrasco
#include <gtest/gtest.h>
#include <lightning/app.h>
#include <lightning/body_parser.h>
#include <lightning/testing.h>

namespace {
using namespace lightning;

TEST (TestingClient, parses_real_requests_through_middleware_and_nested_routes) {
  App app;
  Router api;
  api.use (json());
  api.post ("/:id", [] (const auto &req, auto &res) {
    res.status (201).json ({ { "id", req.params.at ("id") },
      { "query", req.queryParams() }, { "data", *req.jsonBody }, { "host", req.host } });
  });
  app.use ("/api", api);
  lightning::testing::Client client { app };
  HttpHeader headers;
  headers.set ("Content-Type", "application/json");
  headers.set ("Host", "example.test:3000");
  const auto res = client.post ("/api/a%20b?tag=x&tag=y", R"({"ok":true})", headers);
  EXPECT_EQ (res.status, 201);
  EXPECT_EQ (res.json(), (Json { { "id", "a b" }, { "query", { { "tag", { "x", "y" } } } },
    { "data", { { "ok", true } } }, { "host", "example.test" } }));
  EXPECT_EQ (res.headers.get ("Content-Length"), std::to_string (res.body.size()));
  EXPECT_EQ (res.headers.get ("Content-Type"), "application/json; charset=utf-8");
  EXPECT_FALSE (app.running());
}

TEST (TestingClient, supports_all_methods_and_owned_binary_results) {
  Router router;
  for (int i = 0; i < kNumHttpMethods; ++i) {
    const auto method = static_cast<HttpMethod> (i);
    router.addRoute (method, "/", [method] (const auto &req, auto &res) {
      EXPECT_EQ (req.method, method);
      EXPECT_EQ (req.host, "localhost");
      res.send (std::string (req.body.begin(), req.body.end()));
    });
  }
  // HEAD matches GET first, so use a distinct route to inspect its method.
  router.head ("/head", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.method, HttpMethod::kHead); res.send ("abc");
  });
  const Router &routes = router;
  lightning::testing::Client client { routes };
  for (int i = 0; i < kNumHttpMethods; ++i) {
    const auto method = static_cast<HttpMethod> (i);
    if (method == HttpMethod::kHead || method == HttpMethod::kConnect) continue;
    std::string body { "a\0b", 3 };
    const auto res = client.request (method, "/", body);
    body.clear();
    EXPECT_EQ (res.body, (std::string { "a\0b", 3 }));
  }
  const auto head = client.request (HttpMethod::kHead, "/head");
  EXPECT_TRUE (head.body.empty());
  EXPECT_EQ (head.headers.get ("content-length"), "3");
  router.setDefault ([] (const auto &req, auto &res) {
    EXPECT_EQ (req.method, HttpMethod::kConnect);
    EXPECT_EQ (req.url, "example.test:443");
    res.end();
  });
  EXPECT_EQ (client.request (HttpMethod::kConnect, "example.test:443").status, 200);
}

TEST (TestingClient, raw_chunked_messages_and_trailers_use_the_shared_parser) {
  Router router;
  router.post ("/", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.trailers.get ("x-check"), "ok");
    res.send (std::string (req.body.begin(), req.body.end()));
  });
  const auto res = lightning::testing::Client { router }.inject (
    "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\nx-check: ok\r\n\r\n");
  EXPECT_EQ (res.body, "abc");
}

TEST (TestingClient, preserves_head_framing_even_after_method_changes) {
  App app;
  app.use ([] (auto &req, auto &, auto next) { req.method = HttpMethod::kGet; next(); });
  app.get ("/", [] (const auto &, auto &res) { res.send ("hello"); });
  const auto res = lightning::testing::Client { app }.request (HttpMethod::kHead, "/");
  EXPECT_TRUE (res.body.empty());
  EXPECT_EQ (res.headers.get ("content-length"), "5");
}

TEST (TestingClient, errors_fallbacks_and_automatic_options_match_dispatch) {
  App app;
  app.get ("/", [] (const auto &, auto &) { throw std::runtime_error ("failure"); });
  lightning::testing::Client client { app };
  EXPECT_EQ (client.get ("/missing").status, 404);
  EXPECT_EQ (client.get ("/").status, 500);
  EXPECT_THROW (client.get ("/").json(), Json::parse_error);
  const auto options = client.request (HttpMethod::kOptions, "/");
  EXPECT_EQ (options.status, 204);
  EXPECT_TRUE (options.body.empty());
  EXPECT_FALSE (options.headers.contains ("content-length"));
  EXPECT_EQ (options.headers.get ("allow"), "GET, HEAD, OPTIONS");
  app.onError ([] (auto, auto &, auto &res, auto) { res.status (503).json ({ { "retry", true } }); });
  EXPECT_EQ (client.get ("/").json().at ("retry"), true);
  EXPECT_EQ (client.get ("/").status, 503);
}

TEST (TestingClient, rejects_bad_input_and_limits_before_dispatch) {
  App app;
  app.use ([] (auto &, auto &, auto) { FAIL() << "Invalid request dispatched"; });
  lightning::testing::Client client { app, { .bodyBytes = 2 } };
  for (const auto wire : { "", "GET / HTTP/1.1\r\n", "GET / HTTP/1.1\r\n\r\nextra",
      "GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n" })
    EXPECT_THROW (client.inject (wire), std::invalid_argument);
  EXPECT_THROW (client.post ("/", "abc"), std::invalid_argument);
  EXPECT_THROW (client.request (HttpMethod::kUnknown, "/"), std::invalid_argument);
  EXPECT_THROW (client.request (static_cast<HttpMethod> (100), "/"), std::invalid_argument);
  for (const auto target : { "", "/bad path", "/\r\nInjected: x", "/\x7f" })
    EXPECT_THROW (client.get (target), std::invalid_argument);
  for (const auto name : { "Content-Length", "Transfer-Encoding" }) {
    HttpHeader headers;
    headers.set (name, "0");
    EXPECT_THROW (client.get ("/", headers), std::invalid_argument);
  }
  HttpHeader headers;
  headers.set ("x-test", "ok");
  headers.begin()->second = "bad\r\nvalue";
  EXPECT_THROW (client.get ("/", headers), std::invalid_argument);
}

TEST (TestingClient, results_outlive_the_app_and_copies_start_fresh_requests) {
  const auto result = [] {
    App app;
    app.use ([] (auto &req, auto &, auto next) {
      EXPECT_TRUE (req.locals.empty());
      req.locals["visited"] = true;
      next();
    });
    app.get ("/", [] (const auto &, auto &res) {
      res.headers().set ("x-owned", "value");
      res.json ({ { "ok", true } });
    });
    lightning::testing::Client client { app };
    auto copy = client;
    EXPECT_EQ (copy.get ("/").status, 200);
    return client.get ("/");
  }();
  EXPECT_EQ (result.headers.get ("x-owned"), "value");
  EXPECT_EQ (result.json().at ("ok"), true);
}
}
