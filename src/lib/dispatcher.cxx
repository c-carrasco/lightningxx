// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <algorithm>
#include <stdexcept>
#include <lightning/dispatcher.h>

namespace lightning {

// ----------------------------------------------------------------------------
// Dispatcher route management
// ----------------------------------------------------------------------------
void Dispatcher::addRoute (HttpMethod method, std::string_view path, RequestHandler handler) {
  const auto index = static_cast<size_t> (method);
  if (index >= _routes.size() || !handler)
    throw std::invalid_argument ("Invalid HTTP method or empty route handler");
  std::lock_guard<std::mutex> lock { _mutex };
  _routes[index].push_back ({ std::string (path), std::move (handler) });
}

// ----------------------------------------------------------------------------
// Dispatcher default route management
// ----------------------------------------------------------------------------
void Dispatcher::setDefault (RequestHandler handler) {
  std::lock_guard<std::mutex> lock { _mutex };
  _default = std::move (handler);
}

// ----------------------------------------------------------------------------
// Dispatcher request dispatching
// ----------------------------------------------------------------------------
void Dispatcher::dispatch (const HttpRequest &request, HttpResponse &response) const {
  RequestHandler handler;
  {
    std::lock_guard<std::mutex> lock { _mutex };
    const auto index = static_cast<size_t> (request.method);
    if (index < _routes.size()) {
      const auto &routes = _routes[index];
      const auto found = std::find_if (routes.begin(), routes.end(), [&request] (const Route &route) {
        return request.path == route.path;
      });
      handler = found != routes.end() ? found->handler : _default;
    }
  }
  // User code may register routes or change the fallback during dispatch.
  if (handler)
    handler (request, response);
  else
    response.status (404).send ("Not found");
}

}
