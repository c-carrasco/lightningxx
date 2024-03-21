// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_SERVER_H__
#define __LIGHTNING_HTTP_SERVER_H__
#include <cassert>
#include <cstddef>
#include <functional>
#include <list>
#include <string_view>
#include <thread>
#include <vector>

#include <asio.hpp>

#include <lightning/http_method.h>
#include <lightning/http_request.h>
#include <lightning/http_response.h>


namespace lightning {

using RequestHandler = std::function<void (HttpRequest &, HttpResponse &)>;


// ----------------------------------------------------------------------------
// HttpServer Class
// ----------------------------------------------------------------------------
class HttpServer {
  public:
    HttpServer (std::size_t asioPoolSize);
    ~HttpServer();

    void addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler);
    void setDefault (RequestHandler &&handler) { _routeNotFound = handler; }

  private:
    using Route = struct {
      std::string_view path;
      RequestHandler handler;
    };

    asio::io_service _ioService;
    asio::ip::tcp::acceptor _acceptor { _ioService };
    asio::ip::tcp::socket _socket { _ioService };

    std::vector<std::thread> _asioPool;
    std::vector<std::vector<Route>> _routes { static_cast<size_t>(HttpMethod::kPatch) + 1, std::vector<Route>() }; // TODO
    RequestHandler _routeNotFound = nullptr;

    void _acceptNext();
    bool _find (HttpRequest &, RequestHandler &);
};

}

#endif // __LIGHTNING_HTTP_SERVER_H__
