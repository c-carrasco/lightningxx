// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <iostream>

#include <lightning/http_connection.h>
#include <lightning/http_server.h>


namespace lightning {

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
HttpServer::HttpServer (uint16_t port, std::size_t poolSize){
  _logger.transport (cxxlog::transport::OutputStream { std::cout });

  asio::ip::tcp::endpoint ep { asio::ip::tcp::v4(), port };

  ep.address (asio::ip::address::from_string ("127.0.0.1"));

  _acceptor.open (ep.protocol());

  _acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));

  _acceptor.bind (ep);
  _acceptor.listen (asio::socket_base::max_connections);

  _acceptNext();

  _asioPool.reserve (poolSize);
  for (std::size_t i = 0; i < poolSize; ++i)
    _asioPool.emplace_back ([ & ] { _ioService.run(); });

  _logger.info ("Listening, port={}", port);
}

// ----------------------------------------------------------------------------
// Destructor
// ----------------------------------------------------------------------------
HttpServer::~HttpServer () {
  _ioService.post ([ & ] { _acceptor.close(); });

  for (auto &t: _asioPool) {
    t.join();
  }
}

// ----------------------------------------------------------------------------
// HttpServer:addRoute
// ----------------------------------------------------------------------------
void HttpServer::addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler) {
  const auto index { static_cast<decltype(_routes)::size_type> (method) };

  _routes[index].push_back (Route { path, handler });
}



// ----------------------------------------------------------------------------
// HttpServer::_acceptNext
// ----------------------------------------------------------------------------
void HttpServer::_acceptNext () {
  _acceptor.async_accept (
    _socket,
    [ this ] (const auto errCode) {
      _logger.debug ("accepting {} ...", errCode.value());

      if (_acceptor.is_open()) {
        if (!errCode) {
          _logger.debug ("creating connection ...");

          const auto connection = std::make_shared<HttpConnection> (
            std::move (_socket),
            [ this ] (const HttpRequest &request, HttpResponse &response) {
              _logger.debug ("handling connection ...");
              if (const auto handler = _find (request); handler.has_value()) {
                handler.value() (request, response);
              }
              else {
                if (_routeNotFound == nullptr) {
                  response.headers().set ("Content-Type", "text/plain; charset=utf-8");
                  response.status (404).send ("Not found");
                }
                else {
                  _routeNotFound (request, response);
                }
              }
            },
            _logger
          );

          if (connection)
            connection->waitForHttpMessage();
        }

        this->_acceptNext();
      }
    }
  );
}

// ----------------------------------------------------------------------------
// HttpServer::_find
// ----------------------------------------------------------------------------
std::optional<RequestHandler> HttpServer::_find (const HttpRequest &request) const {
  const auto index { static_cast<decltype(_routes)::size_type> (request.method) };
  assert ((index >= 0));

  _logger.debug ("searching {} ...", request.path);

  // FIXME: replace vector by unordered_map
  const auto it = std::find_if (_routes[index].begin(), _routes[index].end(), [ &request ] (const Route &r) {
    return request.path.compare (r.path.data()) == 0;
  });

  if (it == _routes[index].end())
    return std::nullopt;

  return { it->handler };
}

}
