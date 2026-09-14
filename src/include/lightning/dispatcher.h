// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_DISPATCHER_H
#define LIGHTNING_DISPATCHER_H
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include <lightning/http_request.h>
#include <lightning/http_response.h>
#include <lightning/middleware.h>
#include <lightning/async.h>

namespace lightning {
namespace detail { class RoutePattern; }

// Socket-independent, ordered middleware, routes, and mounted routers.
class Dispatcher {
  public:
    void addRoute (HttpMethod method, std::string_view path, RequestHandler handler);
    // Do not erase coroutine return values through synchronous callback types.
    template<detail::CoroutineRouteCallback Handler>
    void setDefault (Handler) = delete;
    template<detail::CoroutineMiddleware Handler>
    void use (Handler) = delete;
    template<detail::CoroutineMiddleware Handler>
    void use (std::string_view, Handler) = delete;
    template<detail::CoroutineErrorHandler Handler>
    void onError (Handler) = delete;
    template<detail::CoroutineRouteCallback Handler>
    void addRoute (HttpMethod, std::string_view, Handler) = delete;
    void addRoute (HttpMethod method, std::string_view path, std::vector<Middleware> handlers);
    void addAsyncRoute (HttpMethod method, std::string_view path, AsyncRequestHandler handler);
    void use (std::string_view prefix, Middleware handler);
    void mount (std::string_view prefix, std::shared_ptr<Dispatcher> router);
    void onError (ErrorHandler handler);
    // An empty handler restores the built-in 404 response.
    void setDefault (RequestHandler handler);
    // Exceptions enter the error chain. Unhandled request decoding/parsing errors
    // produce 400/413/415; other errors produce 500. All close the connection.
    // Const requests are copied.
    void dispatch (HttpRequest &request, HttpResponse &response) const;
    void dispatch (const HttpRequest &request, HttpResponse &response) const;
    // Request/response must outlive the awaited operation. Snapshots are captured
    // when called. Sync dispatch rejects a matched coroutine route via onError.
    Task<> dispatchAsync (HttpRequest &request, HttpResponse &response) const;

  private:
    struct Layer {
      HttpMethod method; // kUnknown denotes prefix middleware.
      std::shared_ptr<const detail::RoutePattern> pattern;
      std::vector<Middleware> handlers;
      std::shared_ptr<Dispatcher> router;
      AsyncRequestHandler asyncHandler {};
    };

    mutable std::mutex _mutex;
    std::vector<std::shared_ptr<const Layer>> _layers;
    std::shared_ptr<const RequestHandler> _default;
    std::vector<std::shared_ptr<const ErrorHandler>> _errors;
    struct Snapshot {
      std::vector<std::shared_ptr<const Layer>> layers;
      std::shared_ptr<const RequestHandler> fallback;
      std::vector<std::shared_ptr<const ErrorHandler>> errors;
    };
    using Snapshots = std::unordered_map<const Dispatcher *, Snapshot>;
    void _capture (Snapshots &snapshots) const;
    friend class detail::Dispatch;
};

}
#endif
