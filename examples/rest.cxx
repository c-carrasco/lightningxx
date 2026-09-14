// ----------------------------------------------------------------------------
// MIT License
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/app.h>
#include <lightning/body_parser.h>

int main() {
  lightning::App app;
  lightning::Router api;
  api.use (lightning::json());
  api.use (lightning::urlencoded());

  api.get ("/search", [] (const auto &req, auto &res) {
    res.json (req.queryParams());
  });

  api.post ("/echo", [] (const auto &req, auto &res) {
    if (req.jsonBody) res.json (*req.jsonBody);
    else if (req.formBody) res.json (*req.formBody);
    else res.status (415).json ({ { "error", "Send JSON or a URL-encoded form" } });
  });

  api.onError ([] (auto error, auto &, auto &res, auto next) {
    try { std::rethrow_exception (error); }
    catch (const lightning::RequestParseError &failure) {
      res.status (failure.status()).json ({ { "error", failure.what() } });
    }
    catch (...) { next (error); }
  });

  app.use ("/api", api);
  app.listen (3000);
}
