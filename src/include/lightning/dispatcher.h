// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_DISPATCHER_H
#define LIGHTNING_DISPATCHER_H
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <lightning/http_request.h>
#include <lightning/http_response.h>
#include <lightning/middleware.h>

namespace lightning {

// Socket-independent, ordered middleware and exact method/path dispatch.
class Dispatcher {
  public:
    void addRoute (HttpMethod method, std::string_view path, RequestHandler handler);
    void addRoute (HttpMethod method, std::string_view path, std::vector<Middleware> handlers);
    void use (std::string_view prefix, Middleware handler);
    void onError (ErrorHandler handler);
    // An empty handler restores the built-in 404 response.
    void setDefault (RequestHandler handler);
    // Callback exceptions enter the error chain. Unhandled errors produce 500 and
    // mark the connection for closure. A const request is dispatched using a copy.
    void dispatch (HttpRequest &request, HttpResponse &response) const;
    void dispatch (const HttpRequest &request, HttpResponse &response) const;

  private:
    struct Layer {
      HttpMethod method; // kUnknown denotes prefix middleware.
      std::string path;
      std::vector<Middleware> handlers;
    };

    mutable std::mutex _mutex;
    std::vector<std::shared_ptr<const Layer>> _layers;
    std::shared_ptr<const RequestHandler> _default;
    std::vector<std::shared_ptr<const ErrorHandler>> _errors;
    friend class detail::Dispatch;
};

}
#endif
