// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <stdexcept>
#include <lightning/app.h>

int main() {
  lightning::App app;

  app.use ("/api", [] (auto &req, auto &res, lightning::Next next) {
    req.locals["source"] = std::string ("example");
    res.headers().set ("x-framework", "lightning");
    next();
  });

  const lightning::Middleware requireBody = [] (auto &req, auto &, auto next) {
    if (req.body.empty())
      throw std::invalid_argument ("Body required");
    next();
  };

  app.post ("/api/echo", requireBody, [] (const auto &req, auto &res) {
    res.send (std::string (req.body.begin(), req.body.end()));
  });

  app.onError ([] (std::exception_ptr error, auto &, auto &res, auto next) {
    try {
      std::rethrow_exception (error);
    }
    catch (const std::invalid_argument &) {
      res.status (400).send ("Body required");
    }
    catch (...) {
      next (error);
    }
  });

  app.listen (3000);
}
