// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_APP_H
#define LIGHTNING_APP_H
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

#include <lightning/dispatcher.h>
#include <lightning/http_server.h>
#include <lightning/router.h>

namespace lightning {

// Configure routes before starting the transport. Registration remains thread-safe
// while serving requests. Handlers follow RequestHandler's concurrency contract.
class App {
  public:
    App() = default;
    App (const App &) = delete;
    ~App();

    App & operator= (const App &) = delete;

    // Do not erase coroutine return values through synchronous callback types.
    template<detail::CoroutineRouteCallback Handler>
    void setDefault (Handler) = delete;

    template<detail::CoroutineMiddleware Handler>
    void use (Handler) = delete;

    template<detail::CoroutineMiddleware Handler>
    void use (std::string_view, Handler) = delete;

    template<detail::CoroutineErrorHandler Handler>
    void onError (Handler) = delete;

    App & addRoute (HttpMethod method, std::string_view path, RequestHandler handler);

    template<detail::CoroutineRouteCallback Handler>
    App & addRoute (HttpMethod, std::string_view, Handler) = delete;

    App & addAsyncRoute (HttpMethod method, std::string_view path, AsyncRequestHandler handler) {
      _dispatcher->addAsyncRoute (method, path, std::move (handler));
      return *this;
    }

    App & getAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kGet, path, std::move (handler));
    }

    App & postAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kPost, path, std::move (handler));
    }

    App & headAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kHead, path, std::move (handler));
    }

    App & putAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kPut, path, std::move (handler));
    }

    App & delAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kDelete, path, std::move (handler));
    }

    App & connectAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kConnect, path, std::move (handler));
    }

    App & optionsAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kOptions, path, std::move (handler));
    }

    App & traceAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kTrace, path, std::move (handler));
    }

    App & patchAsync (std::string_view path, AsyncRequestHandler handler) {
      return addAsyncRoute (HttpMethod::kPatch, path, std::move (handler));
    }

    App & setDefault (RequestHandler handler);

    App & use (Middleware handler);
    App & use (std::string_view prefix, Middleware handler);
    App & use (const Router &router);
    App & use (std::string_view prefix, const Router &router);

    // A separate ordered error chain handles failures from middleware, routes,
    // and the fallback. next() forwards the same error; it never resumes routes.
    App & onError (ErrorHandler handler);

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & addRoute (HttpMethod method, std::string_view path, First first, Rest... rest) {
      _dispatcher->addRoute (method, path, std::vector<Middleware> {
        detail::routeCallback (std::move (first)), detail::routeCallback (std::move (rest))... });
      return *this;
    }

    App & get (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kGet, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & get (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & get (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kGet, path, std::move (first), std::move (rest)...);
    }

    App & head (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kHead, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & head (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & head (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kHead, path, std::move (first), std::move (rest)...);
    }

    App & post (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPost, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & post (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & post (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kPost, path, std::move (first), std::move (rest)...);
    }

    App & put (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPut, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & put (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & put (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kPut, path, std::move (first), std::move (rest)...);
    }

    App & del (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kDelete, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & del (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & del (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kDelete, path, std::move (first), std::move (rest)...);
    }

    App & connect (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kConnect, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & connect (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & connect (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kConnect, path, std::move (first), std::move (rest)...);
    }

    App & options (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kOptions, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & options (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & options (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kOptions, path, std::move (first), std::move (rest)...);
    }

    App & trace (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kTrace, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & trace (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & trace (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kTrace, path, std::move (first), std::move (rest)...);
    }

    App & patch (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPatch, path, std::move (handler));
    }

    template<detail::CoroutineRouteCallback Handler>
    App & patch (std::string_view, Handler) = delete;

    template<detail::RouteCallback First, detail::RouteCallback... Rest>
    App & patch (std::string_view path, First first, Rest... rest) {
      return addRoute (HttpMethod::kPatch, path, std::move (first), std::move (rest)...);
    }

    // Dispatch without a socket, using the same error handling as network requests.
    void dispatch (HttpRequest &request, HttpResponse &response) const;

    // Const requests are copied so middleware can populate request-local state.
    void dispatch (const HttpRequest &request, HttpResponse &response) const;

    Task<> dispatchAsync (HttpRequest &request, HttpResponse &response) const {
      return _dispatcher->dispatchAsync (request, response);
    }

    // Start I/O workers and return after the socket is listening. A second start
    // throws std::logic_error; failed startup leaves the app stopped and reusable.
    App & start (ServerOptions options = {});
    App & start (uint16_t port);

    // Start, then block until another control thread calls stop().
    void listen (ServerOptions options = {});
    void listen (uint16_t port);

    // Idempotent. Waits for handlers and closes connections. Call from a control
    // thread, never a request handler. Join any listen() caller before destruction.
    void stop();

    bool running() const;

    // Actual listening port, or zero when stopped/stopping.
    uint16_t port() const;

  private:
    std::shared_ptr<Dispatcher> _dispatcher { std::make_shared<Dispatcher>() };
    mutable std::mutex _mutex;
    std::condition_variable _stopped;
    std::unique_ptr<HttpServer> _server;
    bool _stopping { false };
    size_t _generation { 0 };
    size_t _completedGeneration { 0 };

    void _start (ServerOptions options);
};

}
#endif
