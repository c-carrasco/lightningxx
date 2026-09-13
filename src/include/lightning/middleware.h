// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_MIDDLEWARE_H
#define LIGHTNING_MIDDLEWARE_H
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <lightning/http_request.h>
#include <lightning/http_response.h>

namespace lightning {
namespace detail { class Dispatch; }

// Synchronous, single-use continuation. Copies share the same invocation state.
// Calling after the callback returns, from another thread, or twice throws logic_error.
class Next {
  public:
    Next() = default;
    void operator() (std::exception_ptr error = nullptr) const;

  private:
    struct State;
    std::shared_ptr<State> _state;
    explicit Next (std::shared_ptr<State> state): _state { std::move (state) } {}
    friend class detail::Dispatch;
};

// Callbacks may execute concurrently for different requests; synchronize shared captures.
using RequestHandler = std::function<void (const HttpRequest &, HttpResponse &)>;
using Middleware = std::function<void (HttpRequest &, HttpResponse &, Next)>;
using ErrorHandler = std::function<void (std::exception_ptr, HttpRequest &, HttpResponse &, Next)>;

namespace detail {
// Adapts existing terminal handlers; returning without send() finishes an empty response.
Middleware asMiddleware (RequestHandler handler);
Middleware asMiddleware (Middleware handler);

template<class Handler>
concept RouteCallback = std::is_constructible_v<RequestHandler, Handler> ||
  std::is_constructible_v<Middleware, Handler>;

template<RouteCallback Handler>
Middleware routeCallback (Handler handler) {
  if constexpr (std::is_constructible_v<Middleware, Handler>)
    return asMiddleware (Middleware { std::move (handler) });
  else
    return asMiddleware (RequestHandler { std::move (handler) });
}
}

}
#endif
