// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <lightning/dispatcher.h>

namespace lightning {

struct Next::State {
  const std::thread::id owner { std::this_thread::get_id() };
  bool active { true };
  bool called { false };
  std::function<void (std::exception_ptr)> resume;
};

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

Middleware asMiddleware (RequestHandler handler) {
  if (!handler)
    throw std::invalid_argument ("Empty route handler");
  return [handler = std::move (handler)] (HttpRequest &request, HttpResponse &response, Next) {
    handler (request, response);
    if (!response.finished())
      response.end();
  };
}

Middleware asMiddleware (Middleware handler) {
  if (!handler)
    throw std::invalid_argument ("Empty middleware");
  return handler;
}

class Dispatch {
  public:
    Dispatch (const std::vector<std::shared_ptr<const Dispatcher::Layer>> &layers,
        const std::shared_ptr<const RequestHandler> &fallback,
        const std::vector<std::shared_ptr<const ErrorHandler>> &errors,
        HttpRequest &request, HttpResponse &response):
      _layers { layers }, _fallback { fallback }, _errors { errors },
      _request { request }, _response { response } {}

    void run (size_t layerIndex = 0, size_t handlerIndex = 0) {
      if (_response.finished())
        throw std::logic_error ("next() cannot continue a finished response");
      while (layerIndex < _layers.size()) {
        const auto &layer = *_layers[layerIndex];
        if (handlerIndex == 0 && !matches (layer)) {
          ++layerIndex;
          continue;
        }
        if (handlerIndex == layer.handlers.size()) {
          ++layerIndex;
          handlerIndex = 0;
          continue;
        }
        try {
          invoke ([&] (Next next) { layer.handlers[handlerIndex] (_request, _response, next); },
            [&, layerIndex, handlerIndex] (std::exception_ptr error) {
              if (error)
                fail (error);
              else
                run (layerIndex, handlerIndex + 1);
            });
        }
        catch (...) { fail (std::current_exception()); }
        return;
      }
      try {
        if (_fallback) {
          (*_fallback) (_request, _response);
          if (!_response.finished())
            _response.end();
        }
        else {
          _response.status (404).send ("Not found");
        }
      }
      catch (...) { fail (std::current_exception()); }
    }

  private:
    const std::vector<std::shared_ptr<const Dispatcher::Layer>> &_layers;
    const std::shared_ptr<const RequestHandler> &_fallback;
    const std::vector<std::shared_ptr<const ErrorHandler>> &_errors;
    HttpRequest &_request;
    HttpResponse &_response;
    size_t _nextError { 0 };

    bool matches (const Dispatcher::Layer &layer) const {
      if (layer.method != HttpMethod::kUnknown)
        return layer.method == _request.method && layer.path == _request.path;
      const auto &prefix = layer.path;
      return prefix == "/" || (_request.path.starts_with (prefix) &&
        (_request.path.size() == prefix.size() || _request.path[prefix.size()] == '/'));
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
      if (!state->called && !_response.finished())
        throw std::logic_error ("Middleware must call next() or finish the response");
    }

    void fail (std::exception_ptr error) {
      // Responses are buffered until dispatch returns. Discard partial application
      // output before starting the error chain, including output preceding a throw.
      _response = HttpResponse {};
      runError (error);
    }

    void runError (std::exception_ptr error) {
      const auto index = _nextError;
      if (index == _errors.size()) {
        _response = HttpResponse {};
        _response.status (500).send ("Internal server error");
        _response._closeAfter = true;
        return;
      }
      // Each error handler runs at most once, including errors thrown while an
      // earlier handler unwinds after next(). Never re-enter an exhausted chain.
      ++_nextError;
      try {
        invoke ([&] (Next next) { (*_errors[index]) (error, _request, _response, next); },
          [&, error] (std::exception_ptr forwarded) {
            if (_response.finished())
              throw std::logic_error ("next() cannot continue a finished response");
            // Error handlers never resume the normal pipeline. next() forwards
            // the current error; next(error) can replace it.
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

void Dispatcher::addRoute (HttpMethod method, std::string_view path, std::vector<Middleware> handlers) {
  if (static_cast<size_t> (method) >= static_cast<size_t> (kNumHttpMethods) || handlers.empty() ||
      std::any_of (handlers.begin(), handlers.end(), [] (const auto &handler) { return !handler; }))
    throw std::invalid_argument ("Invalid HTTP method or empty route handler");
  auto layer = std::make_shared<const Layer> (Layer { method, std::string (path), std::move (handlers) });
  std::lock_guard<std::mutex> lock { _mutex };
  _layers.push_back (std::move (layer));
}

void Dispatcher::use (std::string_view prefix, Middleware handler) {
  if (!handler || prefix.empty() || prefix.front() != '/' ||
      prefix.find_first_of ("?#") != std::string_view::npos || prefix.find ('\0') != std::string_view::npos)
    throw std::invalid_argument ("Middleware needs an absolute path prefix and a nonempty handler");
  while (prefix.size() > 1 && prefix.back() == '/')
    prefix.remove_suffix (1);
  auto layer = std::make_shared<const Layer> (Layer { HttpMethod::kUnknown, std::string (prefix), { std::move (handler) } });
  std::lock_guard<std::mutex> lock { _mutex };
  _layers.push_back (std::move (layer));
}

void Dispatcher::onError (ErrorHandler handler) {
  if (!handler)
    throw std::invalid_argument ("Empty error handler");
  auto owned = std::make_shared<const ErrorHandler> (std::move (handler));
  std::lock_guard<std::mutex> lock { _mutex };
  _errors.push_back (std::move (owned));
}

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
  if (static_cast<size_t> (request.method) >= static_cast<size_t> (kNumHttpMethods)) {
    response.status (404).send ("Not found");
    return;
  }
  std::vector<std::shared_ptr<const Layer>> layers;
  std::shared_ptr<const RequestHandler> fallback;
  std::vector<std::shared_ptr<const ErrorHandler>> errors;
  {
    std::lock_guard<std::mutex> lock { _mutex };
    layers = _layers;
    fallback = _default;
    errors = _errors;
  }
  detail::Dispatch { layers, fallback, errors, request, response }.run();
}

void Dispatcher::dispatch (const HttpRequest &request, HttpResponse &response) const {
  auto copy = request;
  dispatch (copy, response);
}

}
