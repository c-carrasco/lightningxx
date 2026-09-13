// MIT License - Copyright (c) 2026 Carlos Carrasco
#include <algorithm>
#include <iostream>
#include <sstream>
#include <benchmark/benchmark.h>
#include <lightning/app.h>
#include "http_request_parser.h"
#include "build_info.h"

namespace {
using namespace lightning;
const Logger logger { LogLevel::kFatal };
constexpr std::string_view plaintext = "Hello, World!";
bool failed = false;
bool smoke = false;

void require (bool condition, const char *message) {
  if (!condition) throw std::runtime_error (message);
}

template<class Function>
void add (const std::string &name, Function function) {
  auto *test = benchmark::RegisterBenchmark (name.c_str(), [function] (benchmark::State &state) {
    try { function (state); }
    catch (const std::exception &error) { failed = true; state.SkipWithError (error.what()); }
  });
  test->Unit (benchmark::kNanosecond)->UseRealTime();
  if (smoke) test->Iterations (1)->Repetitions (1);
}

std::string binary (size_t size) {
  std::string body (size, '\0');
  for (size_t i = 0; i < size; ++i) body[i] = static_cast<char> (i % 256);
  return body;
}

void parserCase (size_t size, size_t headers, size_t fragment, bool chunked) {
  const auto name = "parser/body=" + std::to_string (size) + "/headers=" + std::to_string (headers) +
    "/fragment=" + std::to_string (fragment) + (chunked ? "/chunked" : "/fixed");
  add (name, [=] (benchmark::State &state) {
    const auto body = binary (size);
    std::string wire = size ? "POST /bench?q=yes HTTP/1.1\r\n" : "GET /bench?q=yes HTTP/1.1\r\n";
    wire += "Host: localhost\r\n";
    for (size_t i = 0; i < headers; ++i) wire += "X-Header-" + std::to_string (i) + ": value\r\n";
    if (chunked) {
      wire += "Transfer-Encoding: chunked\r\n\r\n";
      for (size_t pos = 0; pos < body.size(); pos += 4096) {
        const auto count = std::min<size_t> (4096, body.size() - pos);
        std::ostringstream hex;
        hex << std::hex << count;
        wire += hex.str() + "\r\n" + body.substr (pos, count) + "\r\n";
      }
      wire += "0\r\n\r\n";
    }
    else wire += "Content-Length: " + std::to_string (body.size()) + "\r\n\r\n" + body;
    auto parse = [&] (bool verify) {
      HttpRequest req { std::cref (logger) };
      detail::HttpRequestParser parser { req, { .headerBytes = 65536, .headerCount = 256 } };
      detail::HttpRequestParser::Result result {};
      for (size_t pos = 0; pos < wire.size(); pos += fragment) {
        const auto part = std::string_view { wire }.substr (pos, fragment);
        result = parser.consume (part);
        require (result.status != detail::HttpRequestParser::Status::kInvalid && result.consumed == part.size(), "Parser rejected benchmark input");
      }
      require (result.status == detail::HttpRequestParser::Status::kComplete, "Parser did not complete");
      if (verify) {
        require (req.method == (size ? HttpMethod::kPost : HttpMethod::kGet) &&
          req.version.major == 1 && req.version.minor == 1 && req.host == "localhost", "Incorrect parsed request metadata");
        require (req.path == "/bench" && req.query == "q=yes", "Incorrect parsed target");
        require (req.body.size() == body.size() && std::equal (req.body.begin(), req.body.end(),
          reinterpret_cast<const uint8_t *> (body.data())), "Incorrect parsed binary body");
        require (req.headers.size() == headers + 2, "Incorrect parsed header count");
      }
      benchmark::DoNotOptimize (req.body.data());
      benchmark::DoNotOptimize (req.body.size());
    };
    parse (true); // Full correctness check outside the timed loop.
    for (auto _ : state) { (void) _; parse (false); }
    state.SetBytesProcessed (state.iterations() * static_cast<int64_t> (wire.size()));
    state.SetItemsProcessed (state.iterations());
  });
}

void dispatchCase (size_t routes, std::string position, std::string pattern,
    size_t depth, size_t middleware, bool constant) {
  const auto name = "dispatch/routes=" + std::to_string (routes) + "/" + position + "/" + pattern +
    "/depth=" + std::to_string (depth) + "/middleware=" + std::to_string (middleware) + (constant ? "/const" : "/mutable");
  add (name, [=] (benchmark::State &state) {
    App app;
    for (size_t i = 0; i < middleware; ++i) app.use ([] (auto &, auto &, auto next) { next(); });
    Router leaf;
    std::string prefix;
    if (depth) {
      app.use ("/api", leaf); prefix = "/api";
      for (size_t i = 1; i < depth; ++i) {
        Router child;
        leaf.use ("/api", child); leaf = child; prefix += "/api";
      }
    }
    const size_t selected = position == "first" ? 0 : position == "middle" ? routes / 2 : routes - 1;
    for (size_t i = 0; i < routes; ++i) {
      auto path = "/r" + std::to_string (i);
      if (pattern == "parameter") path += "/:value";
      if (pattern == "wildcard") path += "/*value";
      RequestHandler handler = [pattern] (const auto &req, auto &res) {
        res.send (pattern == "exact" ? std::string { plaintext } : req.params.at ("value"));
      };
      if (depth) leaf.get (path, std::move (handler));
      else app.get (path, std::move (handler));
    }
    HttpRequest req { std::cref (logger) };
    req.method = HttpMethod::kGet;
    req.path = prefix + (position == "missing" ? "/missing" : "/r" + std::to_string (selected));
    if (pattern != "exact") req.path += "/Hello%2C%20World%21";
    req.url = req.path;
    const auto dispatch = [&] (HttpResponse &res) {
      if (constant) app.dispatch (std::as_const (req), res);
      else app.dispatch (req, res);
    };
    HttpResponse check;
    dispatch (check);
    require (check.status() == (position == "missing" ? 404u : 200u), "Incorrect dispatch status");
    require (check.data().ends_with (position == "missing" ? "Not found" : plaintext), "Incorrect dispatch body");
    for (auto _ : state) {
      (void) _;
      HttpResponse res;
      dispatch (res);
      benchmark::DoNotOptimize (res);
    }
    state.SetItemsProcessed (state.iterations());
  });
}

void responseCase (size_t size, size_t headers) {
  add ("response/body=" + std::to_string (size) + "/headers=" + std::to_string (headers), [=] (benchmark::State &state) {
    HttpResponse res;
    const auto body = binary (size);
    res.send (body);
    for (size_t i = 0; i < headers; ++i) res.headers().set ("X-Header-" + std::to_string (i), "value");
    const auto check = res.data();
    require (check.substr (check.find ("\r\n\r\n") + 4) == body, "Incorrect serialized body");
    require (check.find ("content-length: " + std::to_string (size) + "\r\n") != std::string::npos, "Incorrect response length");
    for (auto _ : state) {
      (void) _;
      const auto wire = res.data();
      benchmark::DoNotOptimize (wire.data());
      benchmark::DoNotOptimize (wire.size());
    }
    state.SetBytesProcessed (state.iterations() * static_cast<int64_t> (check.size()));
    state.SetItemsProcessed (state.iterations());
  });
}

void asyncCase (bool yield) {
  add (yield ? "async_dispatch/post" : "async_dispatch/immediate", [=] (benchmark::State &state) {
    App app;
    app.getAsync ("/", [yield] (auto &, auto &res) -> Task<> {
      if (yield) co_await asio::post (co_await asio::this_coro::executor, asio::use_awaitable);
      res.send (std::string { plaintext });
    });
    HttpRequest req { std::cref (logger) };
    req.method = HttpMethod::kGet; req.path = "/";
    asio::io_context io;
    auto run = [&] (bool verify) {
      HttpResponse res;
      std::exception_ptr error;
      bool done = false;
      io.restart();
      asio::co_spawn (io, app.dispatchAsync (req, res), [&] (auto failure) { error = failure; done = true; });
      io.run();
      if (error) std::rethrow_exception (error);
      require (done && res.finished(), "Coroutine dispatch did not finish");
      if (verify) require (res.data().ends_with (plaintext), "Incorrect coroutine response");
      benchmark::DoNotOptimize (res);
    };
    run (true);
    for (auto _ : state) { (void) _; run (false); }
    state.SetItemsProcessed (state.iterations());
  });
}
}

int main (int argc, char **argv) {
  for (int i = 1; i < argc; ++i) if (std::string_view { argv[i] } == "--smoke") {
    smoke = true;
    for (int j = i; j < argc; ++j) argv[j] = argv[j + 1];
    --argc; --i;
  }
  if (!lightning::bench::timingEnabled && !smoke) {
    std::cerr << "Timing requires an uninstrumented Release build; --smoke is correctness-only.\n";
    return 2;
  }
  for (const auto fragment : { 1u, 64u, 4096u }) {
    parserCase (0, 0, fragment, false);
    for (const auto size : { 1024u, 65536u, 1048576u })
      for (const bool chunked : { false, true }) parserCase (size, 0, fragment, chunked);
  }
  for (const auto headers : { 8u, 32u, 128u }) parserCase (0, headers, 4096, false);
  for (const auto routes : { 1u, 10u, 100u, 1000u })
    for (const auto position : { "first", "middle", "last", "missing" })
      for (const auto pattern : { "exact", "parameter", "wildcard" })
        for (const bool constant : { false, true }) dispatchCase (routes, position, pattern, 0, 0, constant);
  for (const auto depth : { 0u, 1u, 4u })
    for (const auto middleware : { 0u, 1u, 5u, 10u })
      for (const bool constant : { false, true }) {
        if (!depth && !middleware) continue; // Already registered above.
        dispatchCase (100, "last", "parameter", depth, middleware, constant);
      }
  for (const auto size : { 0u, 13u, 1024u, 65536u, 1048576u })
    for (const auto headers : { 0u, 8u, 32u }) responseCase (size, headers);
  asyncCase (false); asyncCase (true);
  const auto info = lightning::bench::buildInfo();
  for (const auto &[key, value] : info.items())
    benchmark::AddCustomContext (key, value.is_string() ? value.get<std::string>() : value.dump());
  benchmark::AddCustomContext ("mode", smoke ? "smoke" : "measurement");
  benchmark::Initialize (&argc, argv);
  if (benchmark::ReportUnrecognizedArguments (argc, argv)) return 2;
  const auto matched = benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return failed || matched == 0 ? 1 : 0;
}
