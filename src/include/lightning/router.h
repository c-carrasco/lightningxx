// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_ROUTER_H
#define LIGHTNING_ROUTER_H
#include <lightning/dispatcher.h>
#include <lightning/route_error.h>

namespace lightning {

// A reusable routing handle. Copies and mounts share live configuration.
// Mounts retain their router even after the original handle is destroyed.
class Router {
  public:
    Router() = default;
    // Do not erase coroutine return values through synchronous callback types.
    template<detail::CoroutineRouteCallback Handler>
    void setDefault (Handler) = delete;
    template<detail::CoroutineMiddleware Handler>
    void use (Handler) = delete;
    template<detail::CoroutineMiddleware Handler>
    void use (std::string_view, Handler) = delete;
    template<detail::CoroutineErrorHandler Handler>
    void onError (Handler) = delete;
    Router & addAsyncRoute (HttpMethod method, std::string_view path, AsyncRequestHandler handler) {
      _dispatcher->addAsyncRoute (method, path, std::move (handler));
      return *this;
    }
    Router & getAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kGet, path, std::move (handler));
    }
    Router & postAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kPost, path, std::move (handler));
    }
    Router & headAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kHead, path, std::move (handler));
    }
    Router & putAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kPut, path, std::move (handler));
    }
    Router & delAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kDelete, path, std::move (handler));
    }
    Router & connectAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kConnect, path, std::move (handler));
    }
    Router & optionsAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kOptions, path, std::move (handler));
    }
    Router & traceAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kTrace, path, std::move (handler));
    }
    Router & patchAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kPatch, path, std::move (handler));
    }
    Router (const Router &) = default;
    Router & operator= (const Router &) = default;

    Router & addRoute (HttpMethod method, std::string_view path, RequestHandler handler) {
      _dispatcher->addRoute (method, path, std::move (handler));
      return *this;
    }
    Router & setDefault (RequestHandler handler) {
      _dispatcher->setDefault (std::move (handler));
      return *this;
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & addRoute (HttpMethod, std::string_view, Handler) = delete;
    Router & use (Middleware handler) { return use ("/", std::move (handler)); }
    Router & use (std::string_view prefix, Middleware handler) {
      _dispatcher->use (prefix, std::move (handler));
      return *this;
    }
    Router & use (const Router &router) { return use ("/", router); }
    Router & use (std::string_view prefix, const Router &router) {
      _dispatcher->mount (prefix, router._dispatcher);
      return *this;
    }
    Router & onError (ErrorHandler handler) {
      _dispatcher->onError (std::move (handler));
      return *this;
    }

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & addRoute (HttpMethod method, std::string_view path, First first, Rest... rest) {
      _dispatcher->addRoute (method, path, std::vector<Middleware> {
        detail::routeCallback (std::move (first)), detail::routeCallback (std::move (rest))... });
      return *this;
    }

    Router & get (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kGet, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & get (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & get (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kGet, path, std::move (first), std::move (rest)...);
    }
    Router & head (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kHead, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & head (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & head (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kHead, path, std::move (first), std::move (rest)...);
    }
    Router & post (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPost, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & post (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & post (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kPost, path, std::move (first), std::move (rest)...);
    }
    Router & put (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPut, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & put (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & put (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kPut, path, std::move (first), std::move (rest)...);
    }
    Router & del (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kDelete, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & del (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & del (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kDelete, path, std::move (first), std::move (rest)...);
    }
    Router & connect (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kConnect, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & connect (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & connect (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kConnect, path, std::move (first), std::move (rest)...);
    }
    Router & options (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kOptions, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & options (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & options (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kOptions, path, std::move (first), std::move (rest)...);
    }
    Router & trace (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kTrace, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & trace (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & trace (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kTrace, path, std::move (first), std::move (rest)...);
    }
    Router & patch (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPatch, path, std::move (handler));
    }
    template<detail::CoroutineRouteCallback Handler>
    Router & patch (std::string_view, Handler) = delete;
    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    Router & patch (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kPatch, path, std::move (first), std::move (rest)...);
    }

    void dispatch (HttpRequest &request, HttpResponse &response) const {
      _dispatcher->dispatch (request, response);
    }
    Task<> dispatchAsync (HttpRequest &request, HttpResponse &response) const {
      return _dispatcher->dispatchAsync (request, response);
    }
    void dispatch (const HttpRequest &request, HttpResponse &response) const {
      _dispatcher->dispatch (request, response);
    }

  private:
    std::shared_ptr<Dispatcher> _dispatcher { std::make_shared<Dispatcher>() };
    friend class App;
};

}
#endif
