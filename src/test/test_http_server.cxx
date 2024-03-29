// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <gtest/gtest.h>

#include <cpp20_http_client.hpp>

#include <lightning/http_server.h>


// ----------------------------------------------------------------------------
// test
// ----------------------------------------------------------------------------
TEST (HttpServer, test) {
  lightning::HttpServer server {};

  server.addRoute (lightning::HttpMethod::kGet, "/hello", [] ([[maybe_unused]] const auto &request, auto &response) {
    response.status(200).send ("Hello World!");
  });

  const auto response { http_client::get("http://localhost:8080/hello").send() };
  ASSERT_EQ (response.get_status_code(), http_client::StatusCode::Ok);
  ASSERT_EQ (response.get_http_version(), "HTTP/1.1");
  ASSERT_EQ (response.get_body_string(), "Hello World!");
}