// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <gtest/gtest.h>

#include <lightning/http_server.h>


// ----------------------------------------------------------------------------
// test
// ----------------------------------------------------------------------------
TEST (HttpServer, test) {
  lightning::HttpServer server {};

  server.addRoute (lightning::HttpMethod::kGet, "/hello", [] ([[maybe_unused]] const auto &request, auto &response) {
    response.status(200).send ("Hello World!");
  });
}