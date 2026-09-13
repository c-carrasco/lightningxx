// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_SERVER_H__
#define __LIGHTNING_HTTP_SERVER_H__
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include <asio.hpp>

#include <lightning/types.h>
#include <lightning/http_method.h>
#include <lightning/http_request.h>
#include <lightning/http_response.h>


namespace lightning {

class HttpConnection;

// Handlers run synchronously and may be invoked concurrently for different connections.
using RequestHandler = std::function<void (const HttpRequest &, HttpResponse &)>;

class HttpServer {
  public:
    HttpServer (uint16_t port, size_t poolSize, LogLevel logLevel);

    inline HttpServer (uint16_t port = 8080, LogLevel logLevel = LogLevel::kInfo): HttpServer { port, 1, logLevel } {
      // empty
    }

    // Waits for running handlers, then closes all connections. Destroy from an owning thread.
    ~HttpServer();

    // Configuration can be changed while requests are being served.
    void addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler);
    void setDefault (RequestHandler &&handler);
    // Returns the actual listening port, including when constructed with port zero.
    uint16_t port() const { return _port; }

    inline void setLogLevel (LogLevel level) {
      std::lock_guard<std::mutex> lock { _configMutex };
      _logger.setLevel (level);
    }

  private:
    struct Route {
      std::string path;
      RequestHandler handler;
    };

    Logger _logger;

    asio::io_service _ioService;
    asio::ip::tcp::acceptor _acceptor { asio::make_strand (_ioService) };
    uint16_t _port { 0 };

    std::vector<std::thread> _asioPool;
    std::vector<std::weak_ptr<HttpConnection>> _connections;
    mutable std::mutex _configMutex;
    std::array<std::vector<Route>, kNumHttpMethods> _routes {};
    RequestHandler _routeNotFound = nullptr;

    void _acceptNext();
    std::optional<RequestHandler> _find (const HttpRequest &) const;
};

}

#endif // __LIGHTNING_HTTP_SERVER_H__
