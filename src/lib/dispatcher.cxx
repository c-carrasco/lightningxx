// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <algorithm>
#include <array>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <lightning/dispatcher.h>
#include <lightning/route_error.h>
#include <lightning/request_parse_error.h>
#include "route_pattern.h"

namespace lightning {

// ----------------------------------------------------------------------------
// Next state management for asynchronous middleware
// ----------------------------------------------------------------------------
struct Next::State {
  const std::thread::id owner { std::this_thread::get_id() };
  bool active { true };
  bool called { false };
  std::function<void (std::exception_ptr)> resume;
};

// ----------------------------------------------------------------------------
// Next::operator() implementation
// ----------------------------------------------------------------------------
void Next::operator() (std::exception_ptr error) const {
  // Check the immutable owner before accessing state confined to that thread.
  if (!_state || _state->owner != std::this_thread::get_id())
    throw std::logic_error ("next() must run on its callback thread");
  if (!_state->active || _state->called)
    throw std::logic_error ("next() is expired or has already been called");
  _state->called = true;
  _state->resume (error);
}

namespace detail {

// ----------------------------------------------------------------------------
// Convert a route handler to middleware
// ----------------------------------------------------------------------------
Middleware asMiddleware (RequestHandler handler) {
  if (!handler)
    throw std::invalid_argument ("Empty route handler");
  return [handler = std::move (handler)] (HttpRequest &request, HttpResponse &response, Next) {
    handler (request, response);
    if (!response.finished())
      response.end();
  };
}

// ----------------------------------------------------------------------------
// Convert middleware to middleware (identity)
// ----------------------------------------------------------------------------
Middleware asMiddleware (Middleware handler) {
  if (!handler)
    throw std::invalid_argument ("Empty middleware");
  return handler;
}

// ----------------------------------------------------------------------------
// Dispatch class for managing request dispatching
// ----------------------------------------------------------------------------
class Dispatch {
  using Allowed = std::array<bool, kNumHttpMethods>;
  using Params = std::unordered_map<std::string, std::string>;

  struct ParamsScope {
    Params &target;
    Params previous;
    ParamsScope (Params &target, Params replacement): target { target }, previous { std::move (replacement) } {
      target.swap (previous);
    }
    ~ParamsScope() { target.swap (previous); }
  };

  struct Context {
    std::string path;
    std::string baseUrl;
    Params params;
    explicit Context (const HttpRequest &request): path { request.path }, baseUrl { request.baseUrl }, params { request.params } {}
    void swap (HttpRequest &request) {
      path.swap (request.path);
      baseUrl.swap (request.baseUrl);
      params.swap (request.params);
    }
  };

  struct ContextScope {
    HttpRequest &request;
    Context previous;
    ContextScope (HttpRequest &request, Context replacement): request { request }, previous { std::move (replacement) } {
      previous.swap (request);
    }
    ~ContextScope() { previous.swap (request); }
  };

  struct ErrorContext {
    const Dispatcher::Snapshot *snapshot;
    Context context;
    size_t nextError;
  };
  struct Pending {
    const AsyncRequestHandler *handler;
    Context context;
    std::vector<ErrorContext> errors;
  };
  struct AsyncState {
    std::optional<Pending> pending;
  };

  public:
    Dispatch (const Dispatcher::Snapshot &snapshot, const Dispatcher::Snapshots &snapshots,
        HttpRequest &request, HttpResponse &response,
        std::function<void()> fallthrough = {}, std::function<void (std::exception_ptr)> propagate = {},
        std::shared_ptr<Allowed> allowed = std::make_shared<Allowed>(),
        AsyncState *async = nullptr, std::vector<ErrorContext> ancestors = {}):
      _snapshot { snapshot }, _snapshots { snapshots }, _request { request }, _response { response },
      _inheritedParams { request.params }, _allowed { std::move (allowed) }, _fallthrough { std::move (fallthrough) },
      _propagate { std::move (propagate) }, _async { async }, _ancestors { std::move (ancestors) } {}

    static Task<> runAsync (std::shared_ptr<const Dispatcher::Snapshots> snapshots,
        const Dispatcher *root, HttpRequest &request, HttpResponse &response) {
      if (response.finished()) throw std::logic_error ("Dispatch requires an unfinished response");
      request.params.clear();
      request.baseUrl.clear();
      if (static_cast<size_t> (request.method) >= static_cast<size_t> (kNumHttpMethods)) {
        response.status (404).send ("Not found");
        co_return;
      }
      AsyncState state;
      Dispatch { snapshots->at (root), *snapshots, request, response, {}, {},
        std::make_shared<Allowed>(), &state }.run();
      if (!state.pending) co_return;
      // Retain the handler object itself: coroutine lambda frames refer to their
      // closure, not a copy of its captures. Context scopes survive suspension.
      auto pending = std::move (*state.pending);
      ContextScope selected { request, std::move (pending.context) };
      std::exception_ptr error;
      try {
        auto task = (*pending.handler) (request, response);
        if (!task.valid()) throw std::logic_error ("Coroutine handler returned an empty task");
        co_await std::move (task);
        if (!response.finished()) response.end();
      }
      catch (...) { error = std::current_exception(); }
      const auto cancellation = co_await asio::this_coro::cancellation_state;
      if (cancellation.cancelled() != asio::cancellation_type::none)
        throw asio::system_error (asio::error::operation_aborted);
      if (!error) co_return;
      // Local errors see the handler's current context, as in sync dispatch;
      // propagation to ancestors still restores each mount's parent context.
      pending.errors.back().context = Context { request };
      for (size_t i = pending.errors.size(); i > 0; --i) {
        auto &frame = pending.errors[i - 1];
        ContextScope restored { request, std::move (frame.context) };
        std::exception_ptr forwarded;
        Dispatch dispatch { *frame.snapshot, *snapshots, request, response, {},
          i > 1 ? std::function<void (std::exception_ptr)> { [&] (auto failure) { forwarded = failure; } } : nullptr };
        dispatch._nextError = frame.nextError;
        dispatch.fail (error);
        if (!forwarded) break;
        error = forwarded;
      }
    }

    void run (size_t layerIndex = 0) {
      if (_response.finished())
        throw std::logic_error ("next() cannot continue a finished response");
      while (layerIndex < _snapshot.layers.size()) {
        const auto &layer = *_snapshot.layers[layerIndex];
        try {
          if (_request.method == HttpMethod::kOptions && layer.method != HttpMethod::kUnknown &&
              layer.pattern->match (_request.path))
            allow (layer.method);
          if (layer.method != HttpMethod::kUnknown && layer.method != _request.method &&
              !(layer.method == HttpMethod::kGet && _request.method == HttpMethod::kHead)) {
            ++layerIndex;
            continue;
          }
          const auto match = layer.pattern->match (_request.path);
          if (!match) { ++layerIndex; continue; }
          auto params = _inheritedParams;
          for (const auto &[name, value] : match->params) params[name] = value;
          if (layer.router) {
            mount (layer, match->consumed, std::move (params), layerIndex + 1);
          }
          else {
            ParamsScope scope { _request.params, std::move (params) };
            // Keep captured parameters in scope while handling callback errors.
            if (layer.asyncHandler) {
              if (!_async) throw std::logic_error ("Coroutine routes require dispatchAsync()");
              auto errors = _ancestors;
              errors.push_back ({ &_snapshot, Context { _request }, _nextError });
              _async->pending.emplace (Pending { &layer.asyncHandler, Context { _request }, std::move (errors) });
            }
            else handlers (layer, 0, layerIndex + 1);
          }
        }
        catch (...) { fail (std::current_exception()); }
        return;
      }
      try {
        if (_snapshot.fallback) {
          (*_snapshot.fallback) (_request, _response);
          if (!_response.finished()) _response.end();
        }
        else if (_fallthrough) {
          _fallthrough();
        }
        else if (_request.method == HttpMethod::kOptions && options()) {
          // The automatic response is emitted only after middleware and routers fall through.
        }
        else {
          _response.status (404).send ("Not found");
        }
      }
      catch (...) { fail (std::current_exception()); }
    }

  private:
    const Dispatcher::Snapshot &_snapshot;
    const Dispatcher::Snapshots &_snapshots;
    HttpRequest &_request;
    HttpResponse &_response;
    Params _inheritedParams;
    std::shared_ptr<Allowed> _allowed;
    std::function<void()> _fallthrough;
    std::function<void (std::exception_ptr)> _propagate;
    size_t _nextError { 0 };
    AsyncState *_async;
    std::vector<ErrorContext> _ancestors;

    void allow (HttpMethod method) {
      (*_allowed)[static_cast<size_t> (method)] = true;
      if (method == HttpMethod::kGet) (*_allowed)[static_cast<size_t> (HttpMethod::kHead)] = true;
    }

    bool options() {
      if (_request.path == "*")
        for (const auto &[dispatcher, snapshot] : _snapshots) {
          (void) dispatcher;
          for (const auto &layer : snapshot.layers)
            if (layer->method != HttpMethod::kUnknown) allow (layer->method);
        }
      if (std::none_of (_allowed->begin(), _allowed->end(), [] (bool present) { return present; })) return false;
      allow (HttpMethod::kOptions);
      constexpr std::array names { "GET", "HEAD", "POST", "PUT", "DELETE", "CONNECT", "OPTIONS", "TRACE", "PATCH" };
      std::string value;
      for (size_t i = 0; i < names.size(); ++i) if ((*_allowed)[i]) {
        if (!value.empty()) value += ", ";
        value += names[i];
      }
      _response.headers().set ("allow", value);
      _response.status (204).end();
      return true;
    }

    void handlers (const Dispatcher::Layer &layer, size_t index, size_t nextLayer) {
      if (_response.finished())
        throw std::logic_error ("next() cannot continue a finished response");
      if (index == layer.handlers.size()) {
        ParamsScope scope { _request.params, _inheritedParams };
        run (nextLayer);
        return;
      }
      try {
        invoke ([&] (Next next) { layer.handlers[index] (_request, _response, next); },
          [&, index, nextLayer] (std::exception_ptr error) {
            if (error) fail (error);
            else handlers (layer, index + 1, nextLayer);
          });
      }
      catch (...) { fail (std::current_exception()); }
    }

    void mount (const Dispatcher::Layer &layer, size_t consumed, Params params, size_t nextLayer) {
      const Context parent { _request };
      Context child { _request };
      child.baseUrl += child.path.substr (0, consumed);
      child.path.erase (0, consumed);
      if (child.path.empty()) child.path = "/";
      child.params = std::move (params);
      ContextScope mounted { _request, std::move (child) };
      auto ancestors = _ancestors;
      ancestors.push_back ({ &_snapshot, parent, _nextError });
      Dispatch { _snapshots.at (layer.router.get()), _snapshots, _request, _response,
        [&, nextLayer] {
          ContextScope restored { _request, parent };
          run (nextLayer);
        },
        [&] (std::exception_ptr error) {
          ContextScope restored { _request, parent };
          fail (error);
        }, _allowed, _async, std::move (ancestors)
      }.run();
    }

    template<class Callback, class Resume>
    void invoke (Callback callback, Resume resume) {
      auto state = std::make_shared<Next::State>();
      state->resume = std::move (resume);
      // Even a retained Next must never retain references into this stack frame.
      struct Expire {
        std::shared_ptr<Next::State> state;
        ~Expire() { state->active = false; state->resume = {}; }
      } expire { state };
      callback (Next { state });
      if (_async && _async->pending && _response.finished())
        throw std::logic_error ("Middleware cannot finish a response after selecting a coroutine route");
      if (!state->called && !_response.finished())
        throw std::logic_error ("Middleware must call next() or finish the response");
    }

    void fail (std::exception_ptr error) {
      if (_async) _async->pending.reset();
      _response = HttpResponse {};
      runError (error);
    }

    void runError (std::exception_ptr error) {
      const auto index = _nextError;
      if (index == _snapshot.errors.size()) {
        if (_propagate) { _propagate (error); return; }
        _response = HttpResponse {};
        try {
          std::rethrow_exception (error);
        }
        catch (const RouteDecodeError &) { _response.status (400).send ("Bad request"); }
        catch (const RequestParseError &failure) { _response.status (failure.status()).send (failure.what()); }
        catch (...) { _response.status (500).send ("Internal server error"); }
        _response._closeAfter = true;
        return;
      }
      ++_nextError;
      try {
        invoke ([&] (Next next) { (*_snapshot.errors[index]) (error, _request, _response, next); },
          [&, error] (std::exception_ptr forwarded) {
            if (_response.finished())
              throw std::logic_error ("next() cannot continue a finished response");
            runError (forwarded ? forwarded : error);
          });
      }
      catch (...) {
        _response = HttpResponse {};
        runError (std::current_exception());
      }
    }
};
}

// ----------------------------------------------------------------------------
// Dispatcher registration. Store shared layers so snapshots retain callback
// identity, and no configuration mutex is held while invoking user code.
// ----------------------------------------------------------------------------
void Dispatcher::addRoute (HttpMethod method, std::string_view path, RequestHandler handler) {
  addRoute (method, path, std::vector<Middleware> { detail::asMiddleware (std::move (handler)) });
}

// ----------------------------------------------------------------------------
// Add route to the dispatcher
// ----------------------------------------------------------------------------
void Dispatcher::addAsyncRoute (HttpMethod method, std::string_view path, AsyncRequestHandler handler) {
  if (static_cast<size_t> (method) >= static_cast<size_t> (kNumHttpMethods) || !handler)
    throw std::invalid_argument ("Invalid HTTP method or empty coroutine handler");
  auto pattern = std::make_shared<const detail::RoutePattern> (path, false);
  auto layer = std::make_shared<const Layer> (Layer { method, std::move (pattern), {}, {}, std::move (handler) });
  std::lock_guard<std::mutex> lock { _mutex };
  _layers.push_back (std::move (layer));
}

// ----------------------------------------------------------------------------
// Add asynchronous route to the dispatcher
// ----------------------------------------------------------------------------
void Dispatcher::addRoute (HttpMethod method, std::string_view path, std::vector<Middleware> handlers) {
  if (static_cast<size_t> (method) >= static_cast<size_t> (kNumHttpMethods) || handlers.empty() ||
      std::any_of (handlers.begin(), handlers.end(), [] (const auto &handler) { return !handler; }))
    throw std::invalid_argument ("Invalid HTTP method or empty route handler");
  auto pattern = std::make_shared<const detail::RoutePattern> (path, false);
  auto layer = std::make_shared<const Layer> (Layer { method, std::move (pattern), std::move (handlers), {} });
  std::lock_guard<std::mutex> lock { _mutex };
  _layers.push_back (std::move (layer));
}

// ----------------------------------------------------------------------------
// Add middleware to the dispatcher
// ----------------------------------------------------------------------------
void Dispatcher::use (std::string_view prefix, Middleware handler) {
  if (!handler) throw std::invalid_argument ("Empty middleware");
  auto pattern = std::make_shared<const detail::RoutePattern> (prefix, true);
  auto layer = std::make_shared<const Layer> (Layer { HttpMethod::kUnknown, std::move (pattern), { std::move (handler) }, {} });
  std::lock_guard<std::mutex> lock { _mutex };
  _layers.push_back (std::move (layer));
}

// ----------------------------------------------------------------------------
// Mount a sub-router to the dispatcher
// ----------------------------------------------------------------------------
void Dispatcher::mount (std::string_view prefix, std::shared_ptr<Dispatcher> router) {
  if (!router) throw std::invalid_argument ("Empty router");
  auto pattern = std::make_shared<const detail::RoutePattern> (prefix, true);
  // Only mounts modify graph edges. Serialize cycle detection with edge insertion;
  // ordinary registration and dispatch still use each dispatcher's own mutex.
  static std::mutex graphMutex;
  std::lock_guard<std::mutex> graphLock { graphMutex };
  std::unordered_set<const Dispatcher *> visited;
  std::function<bool (const Dispatcher *)> reachesThis = [&] (const Dispatcher *node) {
    if (node == this) return true;
    if (!visited.insert (node).second) return false;
    std::vector<std::shared_ptr<Dispatcher>> children;
    {
      std::lock_guard<std::mutex> lock { node->_mutex };
      for (const auto &layer : node->_layers)
        if (layer->router) children.push_back (layer->router);
    }
    for (const auto &child : children)
      if (reachesThis (child.get())) return true;
    return false;
  };
  if (reachesThis (router.get())) throw std::invalid_argument ("Router mounts cannot form cycles");
  auto layer = std::make_shared<const Layer> (Layer { HttpMethod::kUnknown, std::move (pattern), {}, std::move (router) });
  std::lock_guard<std::mutex> lock { _mutex };
  _layers.push_back (std::move (layer));
}

// ----------------------------------------------------------------------------
// Capture the current state of the dispatcher
// ----------------------------------------------------------------------------
void Dispatcher::_capture (Snapshots &snapshots) const {
  if (snapshots.contains (this)) return;
  Snapshot snapshot;
  {
    std::lock_guard<std::mutex> lock { _mutex };
    snapshot = { _layers, _default, _errors };
  }
  const auto &stored = snapshots.emplace (this, std::move (snapshot)).first->second;
  for (const auto &layer : stored.layers)
    if (layer->router) layer->router->_capture (snapshots);
}

// ----------------------------------------------------------------------------
// Register error handler
// ----------------------------------------------------------------------------
void Dispatcher::onError (ErrorHandler handler) {
  if (!handler)
    throw std::invalid_argument ("Empty error handler");
  auto owned = std::make_shared<const ErrorHandler> (std::move (handler));
  std::lock_guard<std::mutex> lock { _mutex };
  _errors.push_back (std::move (owned));
}

// ----------------------------------------------------------------------------
// Set default request handler
// ----------------------------------------------------------------------------
void Dispatcher::setDefault (RequestHandler handler) {
  auto owned = handler ? std::make_shared<const RequestHandler> (std::move (handler)) : nullptr;
  std::lock_guard<std::mutex> lock { _mutex };
  _default = std::move (owned);
}

// ----------------------------------------------------------------------------
// Dispatcher request dispatching
// ----------------------------------------------------------------------------
void Dispatcher::dispatch (HttpRequest &request, HttpResponse &response) const {
  if (response.finished())
    throw std::logic_error ("Dispatch requires an unfinished response");
  request.params.clear();
  request.baseUrl.clear();
  if (static_cast<size_t> (request.method) >= static_cast<size_t> (kNumHttpMethods)) {
    response.status (404).send ("Not found");
    return;
  }
  Snapshots snapshots;
  _capture (snapshots);
  detail::Dispatch { snapshots.at (this), snapshots, request, response }.run();
}

// ----------------------------------------------------------------------------
// Dispatcher request dispatching (asynchronous)
// ----------------------------------------------------------------------------
void Dispatcher::dispatch (const HttpRequest &request, HttpResponse &response) const {
  auto copy = request;
  dispatch (copy, response);
}

// ----------------------------------------------------------------------------
// Dispatcher request dispatching (asynchronous) with Task<>
// ----------------------------------------------------------------------------
Task<> Dispatcher::dispatchAsync (HttpRequest &request, HttpResponse &response) const {
  auto snapshots = std::make_shared<Snapshots>();
  _capture (*snapshots);
  return detail::Dispatch::runAsync (std::move (snapshots), this, request, response);
}

}
