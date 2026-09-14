// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <stdexcept>
#include <lightning/app.h>


namespace lightning {

// ----------------------------------------------------------------------------
// App destructor
// ----------------------------------------------------------------------------
App::~App() { stop(); }

// ----------------------------------------------------------------------------
// App::addRoute
// ----------------------------------------------------------------------------
App & App::addRoute (HttpMethod method, std::string_view path, RequestHandler handler) {
  _dispatcher->addRoute (method, path, std::move (handler));
  return *this;
}

// ----------------------------------------------------------------------------
// App::setDefault
// ----------------------------------------------------------------------------
App & App::setDefault (RequestHandler handler) {
  _dispatcher->setDefault (std::move (handler));
  return *this;
}

// ----------------------------------------------------------------------------
// App::use
// ----------------------------------------------------------------------------
App & App::use (Middleware handler) {
  return use ("/", std::move (handler));
}

// ----------------------------------------------------------------------------
// App::use
// ----------------------------------------------------------------------------
App & App::use (std::string_view prefix, Middleware handler) {
  _dispatcher->use (prefix, std::move (handler));
  return *this;
}

// ----------------------------------------------------------------------------
// App::use
// ----------------------------------------------------------------------------
App & App::use (const Router &router) {
  return use ("/", router);
}

// ----------------------------------------------------------------------------
// App::use
// ----------------------------------------------------------------------------
App & App::use (std::string_view prefix, const Router &router) {
  _dispatcher->mount (prefix, router._dispatcher);
  return *this;
}

// ----------------------------------------------------------------------------
// App::onError
// ----------------------------------------------------------------------------
App & App::onError (ErrorHandler handler) {
  _dispatcher->onError (std::move (handler));
  return *this;
}

// ----------------------------------------------------------------------------
// App::dispatch
// ----------------------------------------------------------------------------
void App::dispatch (HttpRequest &request, HttpResponse &response) const {
  _dispatcher->dispatch (request, response);
}

// ----------------------------------------------------------------------------
// App::dispatch
// ----------------------------------------------------------------------------
void App::dispatch (const HttpRequest &request, HttpResponse &response) const {
  _dispatcher->dispatch (request, response);
}

// ----------------------------------------------------------------------------
// App private start helper
// ----------------------------------------------------------------------------
App & App::start (ServerOptions options) {
  std::lock_guard<std::mutex> lock { _mutex };
  _start (std::move (options));
  return *this;
}

// ----------------------------------------------------------------------------
// App convenience start overload
// ----------------------------------------------------------------------------
App & App::start (uint16_t port) {
  ServerOptions options;
  options.port = port;
  return start (std::move (options));
}
// ----------------------------------------------------------------------------
// App listen overloads
// ----------------------------------------------------------------------------
void App::listen (ServerOptions options) {
  std::unique_lock<std::mutex> lock { _mutex };
  _start (std::move (options));
  const auto generation = _generation;
  // A subsequent restart must not keep a previous listen() call blocked.
  _stopped.wait (lock, [this, generation] { return _completedGeneration >= generation; });
}

// ----------------------------------------------------------------------------
// App stop and status
// ----------------------------------------------------------------------------
void App::listen (uint16_t port) {
  ServerOptions options;
  options.port = port;
  listen (std::move (options));
}

// ----------------------------------------------------------------------------
// App stop implementation
// ----------------------------------------------------------------------------
void App::stop() {
  std::unique_lock<std::mutex> lock { _mutex };
  if (_stopping) {
    const auto generation = _generation;
    _stopped.wait (lock, [this, generation] { return _completedGeneration >= generation; });
    return;
  }
  if (!_server)
    return;
  _stopping = true;
  auto server = std::move (_server);
  // A running handler may inspect the app while stop waits for workers to exit.
  lock.unlock();
  server.reset();
  lock.lock();
  _completedGeneration = _generation;
  _stopping = false;
  _stopped.notify_all();
}

// ----------------------------------------------------------------------------
// App running status and port
// ----------------------------------------------------------------------------
bool App::running() const {
  std::lock_guard<std::mutex> lock { _mutex };
  return _server != nullptr;
}

// ----------------------------------------------------------------------------
// App port accessor
// ----------------------------------------------------------------------------
uint16_t App::port() const {
  std::lock_guard<std::mutex> lock { _mutex };
  return _server ? _server->port() : 0;
}

// ----------------------------------------------------------------------------
// App::_start
// ----------------------------------------------------------------------------
void App::_start (ServerOptions options) {
  if (_server || _stopping)
    throw std::logic_error ("The application is already running or stopping");
  _server = std::make_unique<HttpServer> (std::move (options), _dispatcher);
  ++_generation;
}

}
