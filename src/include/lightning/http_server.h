// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_SERVER_H__
#define __LIGHTNING_HTTP_SERVER_H__
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <asio.hpp>

#include <lightning/types.h>
#include <lightning/http_method.h>
#include <lightning/http_request.h>
#include <lightning/http_response.h>
#include <lightning/dispatcher.h>
#include <lightning/transport_options.h>


namespace lightning {

class HttpConnection;

struct ServerOptions {
  uint16_t port { 8080 };
  // Numeric IPv4 or IPv6 address. Loopback is the default.
  std::string address { "127.0.0.1" };
  size_t workers { 1 };
  LogLevel logLevel { LogLevel::kInfo };
  TransportOptions transport;
};

class HttpServer {
  public:
    HttpServer (uint16_t port, size_t poolSize, LogLevel logLevel);
    // The dispatcher is installed before any connection can be accepted.
    HttpServer (ServerOptions options, std::shared_ptr<Dispatcher> dispatcher);

    inline HttpServer (uint16_t port = 8080, LogLevel logLevel = LogLevel::kInfo): HttpServer { port, 1, logLevel } {
      // empty
    }

    // Joins running workers, closes connections, cancels suspended handlers and
    // drains ready cancellations. Destroy from an owning control thread.
    ~HttpServer();

    // Configuration can be changed while requests are being served.
    void addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler);
    template<detail::CoroutineRouteCallback Handler>
    void addRoute (HttpMethod, std::string_view, Handler) = delete;
    void addAsyncRoute (HttpMethod method, std::string_view path, AsyncRequestHandler handler) {
      _dispatcher->addAsyncRoute (method, path, std::move (handler));
    }
    void setDefault (RequestHandler &&handler);
    template<detail::CoroutineRouteCallback Handler>
    void setDefault (Handler) = delete;
    // Returns the actual listening port, including when constructed with port zero.
    uint16_t port() const { return _port; }

    inline void setLogLevel (LogLevel level) {
      std::lock_guard<std::mutex> lock { _configMutex };
      _logger.setLevel (level);
    }

  private:
    Logger _logger;
    std::shared_ptr<Dispatcher> _dispatcher;
    TransportOptions _transport;

    asio::io_service _ioService;
    asio::ip::tcp::acceptor _acceptor { asio::make_strand (_ioService) };
    uint16_t _port { 0 };

    std::vector<std::thread> _asioPool;
    std::vector<std::weak_ptr<HttpConnection>> _connections;
    mutable std::mutex _configMutex;

    void _acceptNext();
};

}

#endif // __LIGHTNING_HTTP_SERVER_H__
