// MIT License - Copyright (c) 2026 Carlos Carrasco
#include <chrono>
#include <asio/steady_timer.hpp>
#include <lightning/app.h>

int main() {
  using namespace std::chrono_literals;
  lightning::App app;
  app.get ("/health", [] (const auto &, auto &res) { res.send ("ok"); });
  app.getAsync ("/hello/:name", [] (auto &req, auto &res) -> lightning::Task<> {
    asio::steady_timer timer { co_await asio::this_coro::executor, 100ms };
    co_await timer.async_wait (asio::use_awaitable);
    res.json ({ { "hello", req.params.at ("name") } });
  });
  // /health can respond while /hello is waiting, even with the default one worker.
  app.listen (3000);
}
