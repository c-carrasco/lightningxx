// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <lightning/http_connection.h>
#include <lightning/http_server.h>

namespace lightning {

HttpServer::HttpServer (uint16_t port, size_t poolSize, LogLevel logLevel): _logger { logLevel } {
  if (poolSize == 0)
    throw std::invalid_argument ("The HTTP server needs at least one worker");
  _logger.transport (cxxlog::transport::OutputStream { std::cout });
  const asio::ip::tcp::endpoint endpoint { asio::ip::address_v4::loopback(), port };
  _acceptor.open (endpoint.protocol());
  _acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));
  _acceptor.bind (endpoint);
  _acceptor.listen (asio::socket_base::max_connections);
  _port = _acceptor.local_endpoint().port();
  _logger.info ("Listening, port={}", _port);
  _acceptNext();
  try {
    _asioPool.reserve (poolSize);
    for (size_t i = 0; i < poolSize; ++i)
      _asioPool.emplace_back ([this] { _ioService.run(); });
  }
  catch (...) {
    _ioService.stop();
    for (auto &thread : _asioPool)
      thread.join();
    throw;
  }
}

HttpServer::~HttpServer() {
  // Stop dispatch before closing sockets so no worker can use them concurrently.
  _ioService.stop();
  for (auto &thread : _asioPool)
    thread.join();
  std::error_code ignored;
  _acceptor.close (ignored);
  for (auto &weak : _connections)
    if (const auto connection = weak.lock())
      connection->close();
}

void HttpServer::addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler) {
  const auto index = static_cast<size_t> (method);
  if (index >= _routes.size() || !handler)
    throw std::invalid_argument ("Invalid HTTP method or empty route handler");
  std::lock_guard<std::mutex> lock { _configMutex };
  _routes[index].push_back (Route { std::string (path), std::move (handler) });
}

void HttpServer::setDefault (RequestHandler &&handler) {
  std::lock_guard<std::mutex> lock { _configMutex };
  _routeNotFound = std::move (handler);
}

void HttpServer::_acceptNext() {
  // Each connection gets its own strand; the acceptor has a separate strand.
  _acceptor.async_accept (asio::make_strand (_ioService),
    [this] (std::error_code ec, asio::ip::tcp::socket socket) {
      if (!_acceptor.is_open())
        return;
      if (!ec) {
        const auto connection = std::make_shared<HttpConnection> (
          std::move (socket),
          [this] (const HttpRequest &request, HttpResponse &response) {
            if (const auto handler = _find (request))
              (*handler) (request, response);
            else
              response.status (404).send ("Not found");
          }, _logger);
        _connections.erase (std::remove_if (_connections.begin(), _connections.end(),
          [] (const auto &weak) { return weak.expired(); }), _connections.end());
        _connections.push_back (connection);
        connection->waitForHttpMessage();
      }
      _acceptNext();
    });
}

std::optional<RequestHandler> HttpServer::_find (const HttpRequest &request) const {
  std::lock_guard<std::mutex> lock { _configMutex };
  const auto index = static_cast<size_t> (request.method);
  if (index >= _routes.size())
    return std::nullopt;
  _logger.debug ("searching {} ...", request.path);
  const auto &routes = _routes[index];
  const auto found = std::find_if (routes.begin(), routes.end(), [&request] (const Route &route) {
    return request.path == route.path;
  });
  if (found != routes.end())
    return found->handler;
  if (_routeNotFound)
    return _routeNotFound;
  return std::nullopt;
}

}
