// Compile and run using only the installed public package and its dependencies.
#include <lightning/app.h>
#include <lightning/body_parser.h>
#include <lightning/testing.h>

int main() {
  lightning::App app;
  lightning::Router api;
  api.use (lightning::json());
  api.postAsync ("/:id", [] (const auto &req, auto &res) -> lightning::Task<> {
    co_await asio::post (co_await asio::this_coro::executor, asio::use_awaitable);
    res.status (201).json ({ { "id", req.params.at ("id") }, { "data", *req.jsonBody } });
  });
  app.use ("/api", api);
  lightning::HttpHeader headers;
  headers.set ("Content-Type", "application/json");
  const auto res = lightning::testing::Client { app }.post ("/api/42", "{\"ok\":true}", headers);
  return res.status == 201 && res.json().at ("id") == "42" &&
    res.json().at ("data").at ("ok") == true ? 0 : 1;
}
