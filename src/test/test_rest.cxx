// ----------------------------------------------------------------------------
// MIT License
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <atomic>
#include <thread>
#include <gtest/gtest.h>
#include <lightning/app.h>
#include <lightning/body_parser.h>
#include "http_test_client.h"

namespace {
using namespace lightning;
Logger logger { LogLevel::kFatal };

std::string body (const HttpResponse &response) {
  const auto wire = response.data();
  return wire.substr (wire.find ("\r\n\r\n") + 4);
}

HttpRequest request (std::string_view data = {}, std::string_view type = "application/json") {
  HttpRequest req { std::cref (logger) };
  req.path = "/";
  req.url = "/";
  req.method = HttpMethod::kPost;
  req.body.assign (data.begin(), data.end());
  if (!type.empty()) req.headers.set ("content-type", type);
  return req;
}

HttpResponse run (const App &app, HttpRequest &req) {
  HttpResponse res;
  app.dispatch (req, res);
  return res;
}

HttpResponse run (const App &app, std::string_view data, std::string_view type = "application/json") {
  auto req = request (data, type);
  return run (app, req);
}

template<class Callback>
void expectParseError (Callback callback, uint32_t status) {
  try { callback(); FAIL() << "Expected RequestParseError"; }
  catch (const RequestParseError &error) { EXPECT_EQ (error.status(), status); }
}

TEST (UrlParameters, repeated_keys_keep_values_in_arrival_order) {
  const auto fields = parseUrlEncoded ("tag=one&tag=two&other=three&tag=");
  EXPECT_EQ (fields.at ("tag"), (std::vector<std::string> { "one", "two", "" }));
  EXPECT_EQ (fields.at ("other"), (std::vector<std::string> { "three" }));
}

TEST (UrlParameters, empty_fields_names_and_values_have_explicit_semantics) {
  EXPECT_TRUE (parseUrlEncoded ("").empty());
  EXPECT_TRUE (parseUrlEncoded ("&&").empty());
  const auto fields = parseUrlEncoded ("&flag&empty=&=value&&=last&");
  EXPECT_EQ (fields.at ("flag").front(), "");
  EXPECT_EQ (fields.at ("empty").front(), "");
  EXPECT_EQ (fields.at (""), (std::vector<std::string> { "value", "last" }));
}

TEST (UrlParameters, decodes_once_after_splitting_and_converts_plus_to_space) {
  const auto fields = parseUrlEncoded ("a%26b=c%3Dd%26e&plus=%2B+%2520&x=a=b;c");
  EXPECT_EQ (fields.at ("a&b").front(), "c=d&e");
  EXPECT_EQ (fields.at ("plus").front(), "+ %20");
  EXPECT_EQ (fields.at ("x").front(), "a=b;c");
}

TEST (UrlParameters, decoded_names_merge_and_brackets_stay_literal) {
  const auto fields = parseUrlEncoded ("a=1&%61=2&user[name]=Ann&items[]=x&__proto__=ok");
  EXPECT_EQ (fields.at ("a"), (std::vector<std::string> { "1", "2" }));
  EXPECT_EQ (fields.at ("user[name]").front(), "Ann");
  EXPECT_EQ (fields.at ("items[]").front(), "x");
  EXPECT_EQ (fields.at ("__proto__").front(), "ok");
}

TEST (UrlParameters, owns_binary_values_and_honors_view_length) {
  std::string raw = "name=%00%FF%C3%A9junk";
  const auto fields = parseUrlEncoded (std::string_view { raw }.substr (0, raw.size() - 4));
  raw.assign (100, 'x');
  EXPECT_EQ (fields.at ("name").front(), (std::string { "\0\xff\xc3\xa9", 4 }));
  const auto literal = parseUrlEncoded (std::string { "x=a\0b", 5 });
  EXPECT_EQ (literal.at ("x").front(), (std::string { "a\0b", 3 }));
}

TEST (UrlParameters, malformed_escapes_in_names_or_values_are_400) {
  for (const auto input : { "%", "%0", "%GG=x", "x=%", "x=%1", "x=%1G", "x=ok&bad=%-1" })
    expectParseError ([&] { parseUrlEncoded (input); }, 400);
}

TEST (UrlParameters, byte_and_field_limits_are_inclusive_and_count_duplicates) {
  EXPECT_EQ (parseUrlEncoded ("x=1", { .limit = 3 }).size(), 1);
  expectParseError ([] { parseUrlEncoded ("x=12", { .limit = 3 }); }, 413);
  EXPECT_EQ (parseUrlEncoded ("x=1&x=2", { .parameterLimit = 2 }).at ("x").size(), 2);
  expectParseError ([] { parseUrlEncoded ("x=1&x=2&x=3", { .parameterLimit = 2 }); }, 413);
  EXPECT_TRUE (parseUrlEncoded ("&&", { .parameterLimit = 0 }).empty());
  expectParseError ([] { parseUrlEncoded ("flag", { .parameterLimit = 0 }); }, 413);
  EXPECT_TRUE (parseUrlEncoded ("", { .limit = 0 }).empty());
}

TEST (RestQuery, parses_current_raw_query_without_changing_request) {
  auto req = request();
  ASSERT_TRUE (req.parse ("GET /search?q=one+two&q=three HTTP/1.1\r\n\r\n"));
  auto values = req.queryParams();
  EXPECT_EQ (values.at ("q").front(), "one two");
  EXPECT_EQ (req.query, "q=one+two&q=three");
  EXPECT_EQ (req.path, "/search");
  req.query = "q=changed";
  EXPECT_EQ (req.queryParams().at ("q").front(), "changed");
  EXPECT_EQ (values.at ("q").back(), "three");
  expectParseError ([&] { req.queryParams ({ .limit = 1 }); }, 413);
}

TEST (RestQuery, lazy_errors_enter_error_chain_only_when_accessed) {
  App app;
  app.post ("/", [] (const auto &req, auto &res) { res.json (req.queryParams()); });
  auto req = request();
  req.query = "x=%GG";
  EXPECT_EQ (run (app, req).status(), 400);
  App raw;
  raw.post ("/", [] (const auto &request, auto &res) { res.send (request.query); });
  EXPECT_EQ (body (run (raw, req)), "x=%GG");
}

TEST (JsonParser, parses_objects_and_preserves_raw_bytes) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &req, auto &res) { res.json (req.jsonBody.value()); });
  const std::string raw = R"({"name":"Ann","tags":[1,true,null],"nul":"\u0000"})";
  auto req = request (raw);
  const auto res = run (app, req);
  EXPECT_EQ (res.status(), 200);
  ASSERT_TRUE (req.jsonBody);
  EXPECT_EQ (req.jsonBody->at ("name"), "Ann");
  EXPECT_EQ (req.jsonBody->at ("nul"), std::string (1, '\0'));
  EXPECT_EQ (std::string (req.body.begin(), req.body.end()), raw);
  EXPECT_EQ (Json::parse (body (res)), *req.jsonBody);
  EXPECT_FALSE (req.formBody);
}

TEST (JsonParser, accepts_every_json_value_and_duplicate_keys_use_last_value) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &req, auto &res) { res.json (req.jsonBody.value()); });
  for (const auto raw : { "null", "true", "42", "-1.5e2", "\"hello\"", "[1,2]", "{}", " {\"x\":1,\"x\":2} \n" }) {
    const auto res = run (app, raw);
    EXPECT_EQ (res.status(), 200) << raw;
    EXPECT_EQ (Json::parse (body (res)), Json::parse (raw));
  }
}

TEST (JsonParser, empty_body_is_unset_but_whitespace_is_invalid) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &req, auto &res) { EXPECT_FALSE (req.jsonBody); res.end(); });
  EXPECT_EQ (run (app, "").status(), 200);
  EXPECT_EQ (run (app, " \r\n\t").status(), 400);
}

TEST (JsonParser, matches_json_suffixes_and_case_insensitive_media_types) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &req, auto &res) { EXPECT_TRUE (req.jsonBody); res.end(); });
  for (const auto type : { "application/json", "Application/JSON ; Charset=\"UTF-8\"",
      "application/problem+json", "application/vnd.example+json; profile=\"a;b\";charset=utf-8" })
    EXPECT_EQ (run (app, "{}", type).status(), 200) << type;
}

TEST (JsonParser, skips_missing_and_unrelated_types_even_when_body_is_large_or_invalid) {
  App app;
  app.use (json ({ .limit = 1 }));
  app.post ("/", [] (const auto &req, auto &res) { EXPECT_FALSE (req.jsonBody); res.end(); });
  for (const auto type : { "", "text/json", "text/plain;charset=unknown", "application/jsonp",
      "application/+json", "application/vnd/bad+json", "application/x-www-form-urlencoded" })
    EXPECT_EQ (run (app, "invalid", type).status(), 200) << type;
}

class InvalidJson: public ::testing::TestWithParam<std::string> {};
TEST_P (InvalidJson, malformed_input_is_400_without_partial_body_or_details) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &, auto &) { FAIL(); });
  auto req = request (GetParam());
  const auto res = run (app, req);
  EXPECT_EQ (res.status(), 400);
  EXPECT_EQ (body (res), "Bad request");
  EXPECT_TRUE (res.shouldClose());
  EXPECT_FALSE (req.jsonBody);
}
INSTANTIATE_TEST_SUITE_P (SyntaxAndEncoding, InvalidJson, ::testing::Values (
  std::string { "{" }, "{\"x\":1,}", "[1,]", "{}{}", "true false", "/*comment*/{}",
  "NaN", "01", "1e9999", "\"\\uD800\"", std::string { "\"\xc0\xaf\"", 4 },
  std::string { "{}\0garbage", 10 }, std::string { "\"a\0b\"", 5 }));

TEST (JsonParser, accepts_utf8_bom_and_surrogate_pairs) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &req, auto &res) { res.json (*req.jsonBody); });
  const auto res = run (app, "\xef\xbb\xbf{\"x\":\"\\uD83D\\uDE00\"}");
  EXPECT_EQ (res.status(), 200);
  EXPECT_EQ (Json::parse (body (res)).at ("x"), "\xf0\x9f\x98\x80");
}

TEST (JsonParser, limits_actual_bytes_independently_of_content_length) {
  App app;
  app.use (json ({ .limit = 2 }));
  app.post ("/", [] (const auto &, auto &res) { res.end(); });
  EXPECT_EQ (run (app, "{}").status(), 200);
  auto req = request ("[1]");
  req.headers.set ("content-length", "1");
  EXPECT_EQ (run (app, req).status(), 413);
  App zero;
  zero.use (json ({ .limit = 0 }));
  zero.post ("/", [] (const auto &, auto &res) { res.end(); });
  EXPECT_EQ (run (zero, "").status(), 200);
  EXPECT_EQ (run (zero, "0").status(), 413);
}

TEST (JsonParser, depth_limit_counts_containers_and_stops_deep_input) {
  EXPECT_THROW (json ({ .maxDepth = 0 }), std::invalid_argument);
  App app;
  app.use (json ({ .maxDepth = 2 }));
  app.post ("/", [] (const auto &, auto &res) { res.end(); });
  EXPECT_EQ (run (app, "{\"x\":[1]}").status(), 200);
  EXPECT_EQ (run (app, "{\"x\":[{}]}").status(), 413);
  EXPECT_EQ (run (app, std::string (1000, '[') + "0" + std::string (1000, ']')).status(), 413);
}

TEST (BodyParsers, reject_unsupported_charset_and_content_encoding) {
  for (const bool isJson : { true, false }) {
    App app;
    app.use (isJson ? json() : urlencoded());
    app.post ("/", [] (const auto &, auto &res) { res.end(); });
    const std::string type = isJson ? "application/json" : "application/x-www-form-urlencoded";
    EXPECT_EQ (run (app, "{}", type + ";charset=iso-8859-1").status(), 415);
    auto req = request (isJson ? "{}" : "x=1", type);
    req.headers.set ("Content-Encoding", "gzip");
    EXPECT_EQ (run (app, req).status(), 415);
    req.headers.set ("Content-Encoding", " Identity ");
    EXPECT_EQ (run (app, req).status(), 200);
  }
}

TEST (BodyParsers, validate_media_parameters_and_quoted_values) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &, auto &res) { res.end(); });
  for (const auto suffix : { ";", ";charset", ";charset=", ";charset=\"utf-8", ";charset=utf-8;CHARSET=utf-8",
      ";x=\"a\"junk", ";x=\"a\r\nb\"", ";x=hello world" })
    if (std::string_view { suffix }.find ('\r') != std::string_view::npos) {
      EXPECT_THROW (run (app, "{}", std::string { "application/json" } + suffix), std::invalid_argument);
    }
    else EXPECT_EQ (run (app, "{}", std::string { "application/json" } + suffix).status(), 400) << suffix;
  EXPECT_EQ (run (app, "{}", "application/json;profile=\"a\\\"b;c\";charset=\"utf-8\"").status(), 200);
}

TEST (FormParser, parses_repeated_values_and_preserves_raw_body) {
  App app;
  app.use (urlencoded());
  app.post ("/", [] (const auto &req, auto &res) { res.json (req.formBody.value()); });
  auto req = request ("name=Ann+Lee&tag=x&tag=y&bytes=%00", "Application/X-Www-Form-Urlencoded; charset=UTF-8");
  const auto raw = req.body;
  const auto res = run (app, req);
  EXPECT_EQ (res.status(), 200);
  ASSERT_TRUE (req.formBody);
  EXPECT_EQ (req.formBody->at ("name").front(), "Ann Lee");
  EXPECT_EQ (req.formBody->at ("tag"), (std::vector<std::string> { "x", "y" }));
  EXPECT_EQ (req.formBody->at ("bytes").front(), std::string (1, '\0'));
  EXPECT_EQ (req.body, raw);
  EXPECT_FALSE (req.jsonBody);
}

TEST (FormParser, empty_matching_body_is_empty_map_and_other_types_are_skipped) {
  App app;
  app.use (urlencoded());
  app.post ("/", [] (const auto &, auto &res) { res.end(); });
  auto req = request ("", "application/x-www-form-urlencoded");
  EXPECT_EQ (run (app, req).status(), 200);
  ASSERT_TRUE (req.formBody);
  EXPECT_TRUE (req.formBody->empty());
  req = request ("bad=%", "text/plain");
  EXPECT_EQ (run (app, req).status(), 200);
  EXPECT_FALSE (req.formBody);
}

TEST (FormParser, malformed_data_and_byte_or_parameter_overflow_enter_error_chain) {
  App app;
  app.use (urlencoded ({ .limit = 7, .parameterLimit = 2 }));
  app.post ("/", [] (const auto &, auto &res) { res.end(); });
  const auto type = "application/x-www-form-urlencoded";
  EXPECT_EQ (run (app, "x=1&x=2", type).status(), 200);
  EXPECT_EQ (run (app, "x=%GG", type).status(), 400);
  EXPECT_EQ (run (app, "a&b&c", type).status(), 413);
  EXPECT_EQ (run (app, "12345678", type).status(), 413);
}

TEST (BodyParsers, are_opt_in_and_can_be_combined_on_a_nested_router) {
  App app;
  Router router;
  router.use (json());
  router.use (urlencoded());
  router.post ("/:id", [] (const auto &req, auto &res) {
    EXPECT_EQ (req.params.at ("id"), "42");
    EXPECT_EQ (req.queryParams().at ("q").front(), "yes");
    res.json (req.jsonBody ? *req.jsonBody : Json (*req.formBody));
  });
  app.use ("/api", router);
  app.post ("/", [] (const auto &req, auto &res) {
    EXPECT_FALSE (req.jsonBody); EXPECT_FALSE (req.formBody); res.send ("raw");
  });
  EXPECT_EQ (body (run (app, "not json")), "raw");
  auto req = request ("{\"x\":1}");
  req.path = "/api/42"; req.query = "q=yes";
  EXPECT_EQ (Json::parse (body (run (app, req))).at ("x"), 1);
  req.headers.set ("content-type", "application/x-www-form-urlencoded");
  req.body = { 'x', '=', '2' };
  EXPECT_EQ (Json::parse (body (run (app, req))).at ("x"), Json::array ({ "2" }));
  EXPECT_FALSE (req.jsonBody);
}

TEST (BodyParsers, route_middleware_order_and_repeated_parsing_follow_current_body) {
  App app;
  app.post ("/", json(), [] (auto &req, auto &, auto next) {
    EXPECT_EQ (*req.jsonBody, 1);
    req.body = { '2' };
    next();
  }, json(), [] (const auto &req, auto &res) { res.json (*req.jsonBody); });
  EXPECT_EQ (body (run (app, "1")), "2");
}

TEST (BodyParsers, parsing_another_http_request_clears_parsed_bodies) {
  auto req = request();
  req.jsonBody = Json { { "old", true } };
  req.formBody = UrlParameters { { "old", { "value" } } };
  ASSERT_TRUE (req.parse ("GET / HTTP/1.1\r\n\r\n"));
  EXPECT_FALSE (req.jsonBody);
  EXPECT_FALSE (req.formBody);
}

TEST (BodyParsers, custom_router_error_handler_can_return_json_and_keep_alive) {
  App app;
  Router router;
  router.use (json());
  router.onError ([] (auto error, auto &, auto &res, auto next) {
    try { std::rethrow_exception (error); }
    catch (const RequestParseError &failure) { res.status (422).json ({ { "status", failure.status() } }); }
    catch (...) { next (error); }
  });
  app.use (router);
  const auto res = run (app, "{");
  EXPECT_EQ (res.status(), 422);
  EXPECT_FALSE (res.shouldClose());
  EXPECT_EQ (Json::parse (body (res)).at ("status"), 400);
}

TEST (BodyParsers, downstream_json_access_errors_remain_application_errors) {
  App app;
  app.use (json());
  app.post ("/", [] (const auto &req, auto &res) { res.json (req.jsonBody->at ("missing")); });
  EXPECT_EQ (run (app, "{}").status(), 500);
}

TEST (JsonResponse, sets_type_preserves_status_and_finishes_with_correct_byte_length) {
  HttpResponse res;
  res.headers().set ("content-type", "text/plain");
  res.headers().set ("x-custom", "yes");
  const Json value = { { "text", "é\n\"" }, { "null", nullptr }, { "list", Json::array ({ 1, true }) } };
  EXPECT_EQ (&res.status (201).json (value), &res);
  EXPECT_EQ (res.status(), 201);
  EXPECT_TRUE (res.finished());
  EXPECT_EQ (res.headers().get ("content-type"), "application/json; charset=utf-8");
  EXPECT_EQ (res.headers().get ("x-custom"), "yes");
  EXPECT_EQ (Json::parse (body (res)), value);
  EXPECT_NE (res.data().find ("content-length: " + std::to_string (body (res).size()) + "\r\n"), std::string::npos);
  const auto head = res.data (true);
  EXPECT_TRUE (head.ends_with ("\r\n\r\n"));
  EXPECT_EQ (head.size() + body (res).size(), res.data().size());
}

TEST (JsonResponse, serializes_strings_null_and_arrays_without_raw_json_ambiguity) {
  HttpResponse string;
  string.json ("{\"x\":1}");
  EXPECT_EQ (Json::parse (body (string)), "{\"x\":1}");
  HttpResponse null;
  null.json (nullptr);
  EXPECT_EQ (body (null), "null");
  HttpResponse array;
  array.json (Json::array ({ 1, 2 }));
  EXPECT_EQ (body (array), "[1,2]");
}

TEST (JsonResponse, owns_serialized_output_and_rejects_changes_after_completion) {
  Json value = { { "x", "original" } };
  HttpResponse res;
  res.json (value);
  value["x"] = "changed";
  EXPECT_EQ (Json::parse (body (res)).at ("x"), "original");
  EXPECT_THROW (res.json (nullptr), std::logic_error);
  EXPECT_THROW (res.send ("again"), std::logic_error);
  EXPECT_THROW (res.status (202), std::logic_error);
  res.headers().set ("x-after", "allowed");
  EXPECT_EQ (res.headers().get ("x-after"), "allowed");
  HttpResponse sent;
  sent.end();
  EXPECT_THROW (sent.json (nullptr), std::logic_error);
}

TEST (JsonResponse, serialization_failure_preserves_open_state_and_dispatch_handles_it) {
  const Json invalid = std::string (1, static_cast<char> (0xff));
  HttpResponse res;
  res.status (202);
  res.headers().set ("content-type", "text/plain");
  EXPECT_THROW (res.json (invalid), Json::type_error);
  EXPECT_FALSE (res.finished());
  EXPECT_EQ (res.status(), 202);
  EXPECT_EQ (res.headers().get ("content-type"), "text/plain");
  EXPECT_NO_THROW (res.send ("recovered"));
  App app;
  app.post ("/", [&] (const auto &, auto &response) { response.json (invalid); });
  EXPECT_EQ (run (app, "").status(), 500);
}

TEST (BodyParsers, shared_middleware_has_no_state_between_concurrent_requests) {
  App app;
  app.use (json());
  app.use (urlencoded());
  app.post ("/", [] (const auto &req, auto &res) {
    res.json (req.jsonBody ? *req.jsonBody : Json (*req.formBody));
  });
  std::atomic<int> failures { 0 };
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) threads.emplace_back ([&, i] {
    const auto value = std::to_string (i);
    for (int j = 0; j < 50; ++j) {
      const bool form = j % 2 == 0;
      const auto res = run (app, form ? "x=" + value : "{\"x\":" + value + "}",
        form ? "application/x-www-form-urlencoded" : "application/json");
      const auto parsed = Json::parse (body (res));
      if (parsed.at ("x") != (form ? Json::array ({ value }) : Json (i))) ++failures;
    }
  });
  for (auto &thread : threads) thread.join();
  EXPECT_EQ (failures, 0);
}

ServerOptions localOptions() {
  ServerOptions options;
  options.port = 0;
  options.workers = 3;
  options.logLevel = LogLevel::kFatal;
  return options;
}

std::string post (std::string_view value, std::string_view type = "application/json") {
  return "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Type: " + std::string (type) +
    "\r\nContent-Length: " + std::to_string (value.size()) + "\r\n\r\n" + std::string (value);
}

TEST (RestSockets, fragmented_json_and_pipelined_form_requests_keep_bodies_separate) {
  App app;
  app.use (json());
  app.use (urlencoded());
  app.post ("/", [] (const auto &req, auto &res) {
    res.json (req.jsonBody ? *req.jsonBody : Json (*req.formBody));
  });
  app.start (localOptions());
  test::Client client { app.port() };
  const auto wire = post ("{\"x\":1}");
  client.send (wire.substr (0, wire.size() - 2));
  client.send (wire.substr (wire.size() - 2) + post ("x=a&x=b", "application/x-www-form-urlencoded"));
  const auto first = client.read();
  EXPECT_EQ (first.status, 200);
  EXPECT_EQ (first.headers.at ("content-type"), "application/json; charset=utf-8");
  EXPECT_EQ (Json::parse (first.body).at ("x"), 1);
  EXPECT_EQ (Json::parse (client.read().body).at ("x"), Json::array ({ "a", "b" }));
}

TEST (RestSockets, chunked_bodies_are_parsed_and_limits_apply_to_assembled_bytes) {
  App app;
  app.use (json ({ .limit = 2 }));
  app.post ("/", [] (const auto &req, auto &res) { res.json (*req.jsonBody); });
  app.start (localOptions());
  test::Client client { app.port() };
  const std::string header = "POST / HTTP/1.1\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n";
  client.send (header + "1\r\n{\r\n1\r\n}\r\n0\r\n\r\n");
  EXPECT_EQ (client.read().body, "{}");
  client.send (header + "2\r\n[1\r\n1\r\n]\r\n0\r\n\r\n");
  EXPECT_EQ (client.read().status, 413);
  EXPECT_TRUE (client.waitForClose());
}

TEST (RestSockets, malformed_json_closes_without_processing_next_pipeline_request) {
  App app;
  std::atomic<int> calls { 0 };
  app.use (json());
  app.post ("/", [&] (const auto &, auto &res) { ++calls; res.end(); });
  app.start (localOptions());
  test::Client client { app.port() };
  client.send (post ("{") + post ("{}"));
  EXPECT_EQ (client.read().status, 400);
  EXPECT_TRUE (client.waitForClose());
  EXPECT_EQ (calls, 0);
}

TEST (RestSockets, json_head_framing_and_next_request_are_preserved) {
  App app;
  app.head ("/", [] (const auto &, auto &res) { res.json ({ { "ok", true } }); });
  app.get ("/", [] (const auto &req, auto &res) {
    EXPECT_FALSE (req.jsonBody); EXPECT_FALSE (req.formBody); res.json (nullptr);
  });
  app.start (localOptions());
  test::Client client { app.port() };
  client.send ("HEAD / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n");
  const auto head = client.read (true);
  EXPECT_EQ (head.headers.at ("content-length"), "11");
  EXPECT_EQ (client.read().body, "null");
}
}
