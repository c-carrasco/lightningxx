// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_APP_H
#define LIGHTNING_APP_H
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

#include <lightning/dispatcher.h>
#include <lightning/http_server.h>

namespace lightning {

// Configure routes before starting the transport. Registration remains thread-safe
// while serving requests. Handlers follow RequestHandler's concurrency contract.
class App {
  public:
    App() = default;
    ~App();
    App (const App &) = delete;
    App & operator= (const App &) = delete;

    App & addRoute (HttpMethod method, std::string_view path, RequestHandler handler);
    App & setDefault (RequestHandler handler);

    App & get (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kGet, path, std::move (handler));
    }
    App & head (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kHead, path, std::move (handler));
    }
    App & post (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPost, path, std::move (handler));
    }
    App & put (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPut, path, std::move (handler));
    }
    App & del (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kDelete, path, std::move (handler));
    }
    App & connect (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kConnect, path, std::move (handler));
    }
    App & options (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kOptions, path, std::move (handler));
    }
    App & trace (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kTrace, path, std::move (handler));
    }
    App & patch (std::string_view path, RequestHandler handler) {
      return addRoute (HttpMethod::kPatch, path, std::move (handler));
    }

    // Dispatch without opening a socket. Handler exceptions propagate to the caller.
    void dispatch (const HttpRequest &request, HttpResponse &response) const;

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
