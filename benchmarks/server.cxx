// MIT License - Copyright (c) 2026 Carlos Carrasco
#include <charconv>
#include <iostream>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <lightning/app.h>
#include "build_info.h"

namespace {
using namespace lightning;
constexpr auto plaintext = "Hello, World!";

unsigned number (std::string_view value, unsigned maximum) {
  unsigned result = 0;
  const auto parsed = std::from_chars (value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || result > maximum)
    throw std::invalid_argument ("Invalid numeric option");
  return result;
}

Json usage() {
  rusage data {};
  if (getrusage (RUSAGE_SELF, &data) != 0) throw std::runtime_error ("getrusage failed");
  const double cpu = data.ru_utime.tv_sec + data.ru_stime.tv_sec +
    (data.ru_utime.tv_usec + data.ru_stime.tv_usec) / 1000000.0;
#ifdef __APPLE__
  const auto peak = static_cast<uint64_t> (data.ru_maxrss);
#else
  const auto peak = static_cast<uint64_t> (data.ru_maxrss) * 1024;
#endif
  return { { "cpu_seconds", cpu }, { "peak_rss_bytes", peak } };
}
}

int main (int argc, char **argv) {
  try {
    ServerOptions options;
    options.port = 3000;
    options.logLevel = LogLevel::kFatal;
    std::string scenario = "plaintext";
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg { argv[i] };
      if (arg == "--help") {
        std::cout << "bench_http_server --scenario plaintext|1k|application|echo|async --workers N --port N --address IP\n"
          "Prints JSON readiness; stdin commands: stats, stop. EOF stops the server.\n";
        return 0;
      }
      if (i + 1 == argc) throw std::invalid_argument ("Missing option value");
      const std::string_view value { argv[++i] };
      if (arg == "--workers") options.workers = number (value, 1024);
      else if (arg == "--port") options.port = static_cast<uint16_t> (number (value, 65535));
      else if (arg == "--address") options.address = value;
      else if (arg == "--scenario") scenario = value;
      else throw std::invalid_argument ("Unknown option");
    }
    App app;
    if (scenario == "plaintext") app.get ("/plaintext", [] (const auto &, auto &res) { res.send (plaintext); });
    else if (scenario == "1k") app.get ("/1k", [body = std::string (1024, 'x')] (const auto &, auto &res) { res.send (body); });
    else if (scenario == "application") {
      for (int i = 0; i < 5; ++i) app.use ([] (auto &, auto &, auto next) { next(); });
      Router api;
      for (int i = 0; i < 100; ++i) api.get ("/users/" + std::to_string (i) + "/:id", [] (const auto &req, auto &res) {
        if (req.params.at ("id") != "42") throw std::runtime_error ("Unexpected workload parameter");
        res.send (plaintext);
      });
      app.use ("/api", api);
    }
    else if (scenario == "echo") app.post ("/echo", [] (const auto &req, auto &res) {
      res.headers().set ("content-type", "application/octet-stream");
      res.send (std::string (req.body.begin(), req.body.end()));
    });
    else if (scenario == "async") app.getAsync ("/async", [] (auto &, auto &res) -> Task<> {
      co_await asio::post (co_await asio::this_coro::executor, asio::use_awaitable);
      res.send (plaintext);
    });
    else throw std::invalid_argument ("Unknown benchmark scenario");
    app.start (options);
    utsname host {};
    if (uname (&host) != 0) throw std::runtime_error ("uname failed");
    std::cout << Json { { "ready", true }, { "port", app.port() }, { "pid", getpid() },
      { "scenario", scenario }, { "workers", options.workers }, { "build", bench::buildInfo() },
      { "system", host.sysname }, { "kernel", host.release }, { "architecture", host.machine },
      { "startup_usage", usage() } }.dump() << std::endl;
    std::string command;
    while (std::getline (std::cin, command) && command != "stop") {
      if (command == "stats") std::cout << usage().dump() << std::endl;
      else throw std::invalid_argument ("Unknown control command");
    }
    app.stop();
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "Benchmark server: " << error.what() << '\n';
    return 1;
  }
}
