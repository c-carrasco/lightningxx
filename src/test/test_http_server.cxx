// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <gtest/gtest.h>

#include <lightning/http_server.h>


// ----------------------------------------------------------------------------
// test_format
// ----------------------------------------------------------------------------
TEST (Logger, test_format) {
  lightning::HttpServer server {};

  server.addRoute (lightning::HttpMethod::kGet, "/hello", [] (const auto &request, auto &response) {
    response.status(200).send ("Hello World!");
  });
}