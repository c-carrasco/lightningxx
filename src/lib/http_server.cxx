// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/http_connection.h>
#include <lightning/http_server.h>


namespace lightning {

// ----------------------------------------------------------------------------
// Constructor
// ----------------------------------------------------------------------------
HttpServer::HttpServer (std::size_t asioPoolSize){
  asio::ip::tcp::endpoint ep { asio::ip::tcp::v4(), 8080 };

  ep.address (asio::ip::address::from_string ("127.0.0.1"));

  _acceptor.open (ep.protocol());

  _acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));

  _acceptor.bind (ep);
  _acceptor.listen (asio::socket_base::max_connections);

  _acceptNext();

  _asioPool.reserve (asioPoolSize);
  for (std::size_t i = 0; i < asioPoolSize; ++i)
    _asioPool.emplace_back ([ & ] { _ioService.run(); });
}

// ----------------------------------------------------------------------------
// Destructor
// ----------------------------------------------------------------------------
HttpServer::~HttpServer () {
  _ioService.post ([ & ] { _acceptor.close(); });

  for (auto &t : _asioPool)
    t.join();
}

// ----------------------------------------------------------------------------
// HttpServer:addRoute
// ----------------------------------------------------------------------------
void HttpServer::addRoute (HttpMethod method, std::string_view path, RequestHandler &&handler) {
  // uint32_t index = static_cast<uint32_t> (method);
  // assert ((index >= 0) && (index < kHttpMethodNumItems));
  const auto index { static_cast<decltype(_routes)::size_type> (method) };

  _routes[index].push_back (Route { path, handler });
}



// ----------------------------------------------------------------------------
// HttpServer::_acceptNext
// ----------------------------------------------------------------------------
void HttpServer::_acceptNext () {
  _acceptor.async_accept (
    _socket,
    [ this ] (auto ec) {
      if (_acceptor.is_open()) {
        if (!ec) {
          auto conn = std::make_shared<HttpConnection> (std::move (_socket), [ this ] (HttpRequest &request, HttpResponse &response) {
            RequestHandler handler;
            if (_find (request, handler)) {
              handler (request, response);
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
          });

          if (conn)
            conn->waitForHttpMessage();
        }

        this->_acceptNext();
      }
    }
  );
}

// ----------------------------------------------------------------------------
// HttpServer::_find
// ----------------------------------------------------------------------------
bool HttpServer::_find (HttpRequest &request, RequestHandler &handler) {
  const auto index { static_cast<decltype(_routes)::size_type> (request.method) };
  assert ((index >= 0));

  // if (index >= kHttpMethodNumItems) {
  //   // TODO: not supported
  // }

  const auto it = std::find_if (_routes[index].begin(), _routes[index].end(), [ & ] (const Route &r) {
    return request.path.compare (r.path.data()) == 0;
  });

  if (it == _routes[index].end())
    return false;

  handler = it->handler;

  return true;
}

}
