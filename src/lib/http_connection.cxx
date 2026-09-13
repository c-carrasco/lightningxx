// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/http_connection.h>
#include "http_request_parser.h"

namespace lightning {

struct HttpConnection::RequestState {
  explicit RequestState (const Logger &logger, RequestLimits limits): request { std::cref (logger) }, parser { request, limits } {}
  HttpRequest request;
  detail::HttpRequestParser parser;
  bool started { false };
};

// ----------------------------------------------------------------------------
// HttpConnection constructor
// ----------------------------------------------------------------------------
HttpConnection::HttpConnection (
    asio::ip::tcp::socket &&socket,
    std::function<void (HttpRequest &, HttpResponse &)> receivedRequest,
    const Logger &logger, TransportOptions options, AsyncRequestHandler asyncReceivedRequest):
  _socket { std::move (socket) },
  _timer { _socket.get_executor() }, _options { options },
  _onReceivedRequest { std::move (receivedRequest) },
  _onReceivedAsync { std::move (asyncReceivedRequest) },
  _logger { logger },
  _state { std::make_unique<RequestState> (logger, options.limits) }
{}

// ----------------------------------------------------------------------------
// HttpConnection close
// ----------------------------------------------------------------------------
void HttpConnection::close() {
  if (_closed) return;
  _closed = true;
  _cancelDeadline();
  std::error_code ignored;
  _socket.close (ignored);
  _handlerCancellation.emit (asio::cancellation_type::terminal);
}

// ----------------------------------------------------------------------------
// HttpConnection cancel deadline
// ----------------------------------------------------------------------------
void HttpConnection::_cancelDeadline() {
  ++_timerGeneration;
  _timer.cancel();
}

// ----------------------------------------------------------------------------
// HttpConnection deadline
// ----------------------------------------------------------------------------
void HttpConnection::_deadline (Phase phase, std::chrono::milliseconds duration) {
  _phase = phase;
  _cancelDeadline();
  if (duration.count() == 0) return;
  _timer.expires_after (duration);
  _timer.async_wait ([ctx = shared_from_this(), generation = _timerGeneration] (std::error_code ec) {
    if (!ec && !ctx->_closed && generation == ctx->_timerGeneration) ctx->close();
  });
}

// ----------------------------------------------------------------------------
// HttpConnection wait for HTTP message
// ----------------------------------------------------------------------------
void HttpConnection::waitForHttpMessage() {
  asio::dispatch (_socket.get_executor(), [ctx = shared_from_this()] {
    if (ctx->_closed) return;
    ctx->_deadline (Phase::kHeader, ctx->_options.headerTimeout);
    ctx->_waitForMessage();
  });
}

// ----------------------------------------------------------------------------
// HttpConnection wait for message (internal)
// ----------------------------------------------------------------------------
void HttpConnection::_waitForMessage() {
  if (_closed) return;
  if (_inputBuffer.length() != 0)
    _consumeData (_inputBuffer.bytes(), _inputBuffer.length());
  else
    _consumeMessage();
}

// ----------------------------------------------------------------------------
// HttpConnection consume message (internal)
// ----------------------------------------------------------------------------
void HttpConnection::_consumeMessage() {
  _socket.async_read_some (
    _inputBuffer.makeAsioBuffer(),
    [ctx = shared_from_this()] (auto ec, size_t length) {
      ctx->_afterRead (ec, length);
    }
  );
}

// ----------------------------------------------------------------------------
// HttpConnection after read (internal)
// ----------------------------------------------------------------------------
void HttpConnection::_afterRead (const std::error_code &ec, size_t length) {
  if (_closed) return;
  if (length != 0) {
    _inputBuffer.obtainedBytes (length);
    _consumeData (_inputBuffer.bytes(), length);
  }
  else if (ec == asio::error::eof && _state->started) {
    HttpResponse response;
    response.status (400).send ("Bad request");
    _writeResponseMessage (std::move (response), true, _state->request.method == HttpMethod::kHead);
  }
  else {
    close();
  }
}

// ----------------------------------------------------------------------------
// HttpConnection consume data (internal)
// ----------------------------------------------------------------------------
void HttpConnection::_consumeData (const char *data, size_t length) {
  bool omitBody = false;

  try {
    if (_phase == Phase::kIdle)
      _deadline (Phase::kHeader, _options.headerTimeout);

    _state->started = true;
    const auto result = _state->parser.consume ({ data, length });
    // Middleware may rewrite the method for routing. Wire framing must still
    // follow the method parsed from the client's original request.
    omitBody = _state->request.method == HttpMethod::kHead;
    _inputBuffer.consumedBytes (result.consumed);
    if (result.status == detail::HttpRequestParser::Status::kInvalid) {
      HttpResponse response;
      const auto status = result.errorStatus;
      response.status (status).send (status == 413 ? "Payload too large" : status == 414 ? "URI too long" :
        status == 431 ? "Request headers too large" : status == 417 ? "Expectation failed" :
        status == 505 ? "HTTP version not supported" : "Bad request");
      _writeResponseMessage (std::move (response), true, omitBody);
      return;
    }

    if (result.status == detail::HttpRequestParser::Status::kIncomplete) {
      if (_state->parser.headersComplete() && _phase != Phase::kBody)
        _deadline (Phase::kBody, _options.bodyTimeout);
      _consumeMessage();
      return;
    }

    _cancelDeadline();
    auto &request = _state->request;
    std::error_code ec;
    const auto endpoint = _socket.remote_endpoint (ec);
    if (ec) {
      close();
      return;
    }

    request.ip = endpoint.address().to_string();
    request.protocol = ProtocolType::kHttp;
    _deadline (Phase::kHandler, _options.handlerTimeout);

    asio::co_spawn (_socket.get_executor(), _dispatchRequest (result.keepAlive, omitBody),
      asio::bind_cancellation_slot (_handlerCancellation.slot(),
        [ctx = shared_from_this()] (std::exception_ptr error) {
          if (error) ctx->close();
        }));
  }
  catch (...) {
    // Discard any partially constructed application response.
    HttpResponse response;
    response.status (500).send ("Internal server error");
    _writeResponseMessage (std::move (response), true, omitBody);
  }
}

// ----------------------------------------------------------------------------
// HttpConnection dispatch request (internal)
// ----------------------------------------------------------------------------
Task<> HttpConnection::_dispatchRequest (bool keepAlive, bool omitBody) {
  if (_closed)
    co_return;

  HttpResponse response;
  bool failed = false;
  try {
    if (_onReceivedAsync) {
      auto task = _onReceivedAsync (_state->request, response);
      if (!task.valid()) throw std::logic_error ("Request callback returned an empty task");
      co_await std::move (task);
    }
    else _onReceivedRequest (_state->request, response);
  }
  catch (...) {
    response = HttpResponse {};
    response.status (500).send ("Internal server error");
    failed = true;
  }

  if (_closed)
    co_return;

  const bool closeAfter = failed || !keepAlive || response.shouldClose();
  if (!closeAfter && _state->request.version.minor == 0 && !response.headers().contains ("connection"))
    response.headers().set ("connection", "keep-alive");
  try {
    _writeResponseMessage (std::move (response), closeAfter, omitBody);
  }
  catch (...) {
    HttpResponse fallback;
    fallback.status (500).send ("Internal server error");
    _writeResponseMessage (std::move (fallback), true, omitBody);
  }
}

// ----------------------------------------------------------------------------
// HttpConnection write response message (internal)
// ----------------------------------------------------------------------------
void HttpConnection::_writeResponseMessage (HttpResponse response, bool closeAfter, bool omitBody) {
  if (_closed)
    return;

  if (const auto connection = response.headers().get ("connection")) {
    std::string tokens { *connection };
    for (auto &c : tokens) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    size_t pos = 0;
    while (pos < tokens.size()) {
      const auto end = tokens.find (',', pos);
      auto part = std::string_view { tokens }.substr (pos, end == std::string::npos ? end : end - pos);
      while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix (1);
      while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix (1);
      if (part == "close") closeAfter = true;
      if (end == std::string::npos) break;
      pos = end + 1;
    }
  }

  if (closeAfter)
    response.headers().set ("connection", "close");

  const auto data = std::make_shared<const std::string> (response.data (omitBody));
  _deadline (Phase::kWrite, _options.writeTimeout);

  asio::async_write (
    _socket,
    asio::buffer (*data),
    [ctx = shared_from_this(), data, closeAfter] (std::error_code ec, size_t) {
      if (ctx->_closed) return;
      ctx->_cancelDeadline();
      if (ec || closeAfter) {
        ctx->close();
        return;
      }
      ctx->_state = std::make_unique<RequestState> (ctx->_logger.get(), ctx->_options.limits);
      ctx->_deadline (Phase::kIdle, ctx->_options.keepAliveTimeout);
      ctx->_waitForMessage();
    }
  );
}

}
