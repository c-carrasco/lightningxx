// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/app.h>

int main() {
  lightning::App app;

  app.get ("/", [] (const auto &, auto &res) {
    res.send ("Hello World!");
  });

  app.post ("/echo", [] (const auto &req, auto &res) {
    res.send (std::string (req.body.begin(), req.body.end()));
  });

  app.listen (3000);
}
