// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/app.h>

int main() {
  lightning::App app;
  lightning::Router api;
  lightning::Router users;

  users.get ("/:id", [] (const auto &req, auto &res) {
    res.send ("Organization " + req.params.at ("org") + ", user " + req.params.at ("id"));
  });
  api.use ("/orgs/:org/users", users);
  api.get ("/files/*path", [] (const auto &req, auto &res) {
    res.send ("Requested path: " + req.params.at ("path"));
  });

  app.use ("/api", api);
  app.listen (3000);
}
