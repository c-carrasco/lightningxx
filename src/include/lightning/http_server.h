// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_SERVER_H__
#define __LIGHTNING_HTTP_SERVER_H__
#include <functional>
#include <string_view>
#include <thread>
#include <vector>

#include <asio.hpp>

#include <cxxlog/logger.h>
#include <cxxlog/transport.h>

#include <lightning/http_method.h>
#include <lightning/http_request.h>
#include <lightning/http_response.h>


namespace lightning {

using RequestHandler = std::function<void (const HttpRequest &, HttpResponse &)>;

class HttpServer {
  public:
    HttpServer (uint16_t port=8080, size_t poolSize = 1);
    ~HttpServer();

    void addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler);
    void setDefault (RequestHandler &&handler) { _routeNotFound = handler; }

  private:
    struct Route {
      std::string_view path;
      RequestHandler handler;
    };

    cxxlog::Logger<cxxlog::transport::OutputStream> _logger { cxxlog::Severity::kVerbose };

    asio::io_service _ioService;
    asio::ip::tcp::acceptor _acceptor { _ioService };
    asio::ip::tcp::socket _socket { _ioService };

    std::vector<std::thread> _asioPool;
    std::array<std::vector<Route>, kNumHttpMethods> _routes {};
    RequestHandler _routeNotFound = nullptr;

    void _acceptNext();
    std::optional<RequestHandler> _find (const HttpRequest &) const;
};

}

#endif // __LIGHTNING_HTTP_SERVER_H__
