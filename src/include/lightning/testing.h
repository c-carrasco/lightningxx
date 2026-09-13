// MIT License - Copyright (c) 2026 Carlos Carrasco
#ifndef LIGHTNING_TESTING_H
#define LIGHTNING_TESTING_H
#include <functional>
#include <lightning/http_request.h>
#include <lightning/http_response.h>
#include <lightning/async.h>

namespace lightning::testing {

// Owned serialized response, independent of the client and application lifetime.
struct Response {
  uint32_t status;
  HttpHeader headers;
  std::string body;
  Json json() const { return Json::parse (body); }
};

// Blocking, socket-free application tests without a test-framework dependency.
// The App/Router/Dispatcher must outlive this client. No transport timers or
// connection persistence are simulated; use socket tests for those behaviors.
class Client {
  public:
    template<class Routes>
      requires requires (Routes &routes, HttpRequest &req, HttpResponse &res) { routes.dispatchAsync (req, res); }
    explicit Client (Routes &routes, RequestLimits limits = {}):
      _dispatch { [&routes] (HttpRequest &req, HttpResponse &res) { return routes.dispatchAsync (req, res); } },
      _limits { limits } {}

    // Invalid/incomplete/multiple HTTP messages or receive-limit failures throw
    // invalid_argument before dispatch. Application errors use the error chain.
    Response inject (std::string_view wire) const;
    // Content-Length is computed from body bytes. Custom framing is rejected;
    // use inject() for raw/chunked requests. Host defaults to localhost.
    Response request (HttpMethod method, std::string_view target,
      std::string_view body = {}, const HttpHeader &headers = {}) const;
    Response get (std::string_view target, const HttpHeader &headers = {}) const {
      return request (HttpMethod::kGet, target, {}, headers);
    }
    Response post (std::string_view target, std::string_view body,
      const HttpHeader &headers = {}) const {
      return request (HttpMethod::kPost, target, body, headers);
    }

  private:
    AsyncRequestHandler _dispatch;
    RequestLimits _limits;
};
}
#endif
