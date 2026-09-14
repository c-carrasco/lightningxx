// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <algorithm>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <lightning/http_connection.h>
#include <lightning/http_server.h>


namespace lightning {

// ----------------------------------------------------------------------------
// HTTP server construction and destruction
// ----------------------------------------------------------------------------
HttpServer::HttpServer (uint16_t port, size_t poolSize, LogLevel logLevel):
  HttpServer { ServerOptions { port, "127.0.0.1", poolSize, logLevel, {} }, std::make_shared<Dispatcher>() }
{
  // empty
}

// ----------------------------------------------------------------------------
// HTTP server construction and destruction
// ----------------------------------------------------------------------------
HttpServer::HttpServer (ServerOptions options, std::shared_ptr<Dispatcher> dispatcher):
  _logger { options.logLevel },
  _dispatcher { std::move (dispatcher) },
  _transport { options.transport }
{
  if (options.workers == 0)
    throw std::invalid_argument ("The HTTP server needs at least one worker");

  if (!_dispatcher)
    throw std::invalid_argument ("The HTTP server needs a dispatcher");

  if (
    (_transport.headerTimeout.count() < 0) ||
    (_transport.bodyTimeout.count() < 0) ||
    (_transport.writeTimeout.count() < 0) ||
    (_transport.keepAliveTimeout.count() < 0) ||
    (_transport.handlerTimeout.count() < 0)
  ) {
    throw std::invalid_argument ("Transport timeouts cannot be negative");
  }

  _logger.transport (cxxlog::transport::OutputStream { std::cout });

  const asio::ip::tcp::endpoint endpoint { asio::ip::make_address (options.address), options.port };

  _acceptor.open (endpoint.protocol());
  _acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));
  _acceptor.bind (endpoint);
  _acceptor.listen (asio::socket_base::max_connections);
  _port = _acceptor.local_endpoint().port();
  _logger.info ("Listening, port={}", _port);
  _acceptNext();

  // Preconfigured handlers may inspect App, whose startup lock is still held.
  // Do not dispatch any user code until every worker was successfully created:
  // otherwise a partial startup failure could deadlock while joining workers.
  const auto ready = std::make_shared<std::latch> (1);
  try {
    _asioPool.reserve (options.workers);
    for (size_t i = 0; i < options.workers; ++i)
      _asioPool.emplace_back ([this, ready] {
        ready->wait();
        _ioService.run();
      });
  }
  catch (...) {
    _ioService.stop();
    ready->count_down();

    for (auto &thread : _asioPool)
      thread.join();

    throw;
  }

  ready->count_down();
}

// ----------------------------------------------------------------------------
// HTTP server destruction
// ----------------------------------------------------------------------------
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

  // Deliver terminal cancellation while the dispatcher and logger are alive.
  // Operations that ignore cancellation are destroyed with the I/O context.
  _ioService.restart();
  _ioService.poll();
  _ioService.stop();
}

// ----------------------------------------------------------------------------
// HTTP server route management
// ----------------------------------------------------------------------------
void HttpServer::addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler) {
  _dispatcher->addRoute (method, path, std::move (handler));
}

// ----------------------------------------------------------------------------
// HTTP server default route management
// ----------------------------------------------------------------------------
void HttpServer::setDefault (RequestHandler &&handler) {
  _dispatcher->setDefault (std::move (handler));
}

// ----------------------------------------------------------------------------
// HTTP server internal accept loop
// ----------------------------------------------------------------------------
void HttpServer::_acceptNext() {
  // Each connection gets its own strand; the acceptor has a separate strand.
  _acceptor.async_accept (asio::make_strand (_ioService),
    [this] (std::error_code ec, asio::ip::tcp::socket socket) {
      if (!_acceptor.is_open())
        return;

      if (!ec) {
        const auto connection = std::make_shared<HttpConnection> (
          std::move (socket),
          std::function<void (HttpRequest &, HttpResponse &)> {},
          _logger,
          _transport,
          [dispatcher = _dispatcher] (HttpRequest &request, HttpResponse &response) {
            return dispatcher->dispatchAsync (request, response);
          }
        );

        _connections.erase (std::remove_if (
          _connections.begin(),
          _connections.end(),
          [] (const auto &weak) { return weak.expired(); }),
          _connections.end()
        );
        _connections.push_back (connection);
        connection->waitForHttpMessage();
      }

      _acceptNext();
    });
}

}
