// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_DISPATCHER_H
#define LIGHTNING_DISPATCHER_H
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <lightning/http_request.h>
#include <lightning/http_response.h>

namespace lightning {

// Handlers run synchronously and may be invoked concurrently for different requests.
using RequestHandler = std::function<void (const HttpRequest &, HttpResponse &)>;

// Socket-independent dispatch. The first exact method/path match wins.
class Dispatcher {
  public:
    void addRoute (HttpMethod method, std::string_view path, RequestHandler handler);
    // An empty handler restores the built-in 404 response.
    void setDefault (RequestHandler handler);
    // Exceptions propagate to the caller; HttpConnection supplies the HTTP 500 boundary.
    void dispatch (const HttpRequest &request, HttpResponse &response) const;

  private:
    struct Route {
      std::string path;
      RequestHandler handler;
    };

    mutable std::mutex _mutex;
    std::array<std::vector<Route>, kNumHttpMethods> _routes;
    RequestHandler _default;
};

}
#endif
