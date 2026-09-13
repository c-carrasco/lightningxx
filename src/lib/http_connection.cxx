// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/http_connection.h>
#include "http_request_parser.h"

namespace lightning {

struct HttpConnection::RequestState {
  explicit RequestState (const Logger &logger): request { std::cref (logger) }, parser { request } {}
  HttpRequest request;
  detail::HttpRequestParser parser;
  bool started { false };
};

HttpConnection::HttpConnection (
    asio::ip::tcp::socket &&socket,
    std::function<void (HttpRequest &, HttpResponse &)> receivedRequest,
    const Logger &logger):
  _socket { std::move (socket) },
  _onReceivedRequest { std::move (receivedRequest) },
  _logger { logger },
  _state { std::make_unique<RequestState> (logger) }
{}

HttpConnection::~HttpConnection() = default;

void HttpConnection::close() {
  std::error_code ignored;
  _socket.close (ignored);
}

void HttpConnection::waitForHttpMessage() {
  if (_inputBuffer.length() != 0)
    _consumeData (_inputBuffer.bytes(), _inputBuffer.length());
  else
    _consumeMessage();
}

void HttpConnection::_consumeMessage() {
  _socket.async_read_some (
    _inputBuffer.makeAsioBuffer(),
    [ctx = shared_from_this()] (auto ec, size_t length) {
      ctx->_afterRead (ec, length);
    }
  );
}

void HttpConnection::_afterRead (const std::error_code &ec, size_t length) {
  if (length != 0) {
    _inputBuffer.obtainedBytes (length);
    _consumeData (_inputBuffer.bytes(), length);
  }
  else if (ec == asio::error::eof && _state->started) {
    HttpResponse response;
    response.status (400).send ("Bad request");
    _writeResponseMessage (std::move (response), true);
  }
  else {
    close();
  }
}

void HttpConnection::_consumeData (const char *data, size_t length) {
  bool omitBody = false;
  try {
    _state->started = true;
    const auto result = _state->parser.consume ({ data, length });
    // Middleware may rewrite the method for routing. Wire framing must still
    // follow the method parsed from the client's original request.
    omitBody = _state->request.method == HttpMethod::kHead;
    _inputBuffer.consumedBytes (result.consumed);
    if (result.status == detail::HttpRequestParser::Status::kInvalid) {
      HttpResponse response;
      response.status (400).send ("Bad request");
      _writeResponseMessage (std::move (response), true);
      return;
    }
    if (result.status == detail::HttpRequestParser::Status::kIncomplete) {
      _consumeMessage();
      return;
    }

    auto &request = _state->request;
    std::error_code ec;
    const auto endpoint = _socket.remote_endpoint (ec);
    if (ec) {
      close();
      return;
    }
    request.ip = endpoint.address().to_string();
    request.protocol = ProtocolType::kHttp;
    HttpResponse response;
    _onReceivedRequest (request, response);
    const bool closeAfter = !result.keepAlive || response.shouldClose();
    _writeResponseMessage (std::move (response), closeAfter, omitBody);
  }
  catch (...) {
    // Discard any partially constructed application response.
    HttpResponse response;
    response.status (500).send ("Internal server error");
    _writeResponseMessage (std::move (response), true, omitBody);
  }
}

void HttpConnection::_writeResponseMessage (HttpResponse response, bool closeAfter, bool omitBody) {
  if (closeAfter)
    response.headers().set ("connection", "close");
  const auto data = std::make_shared<const std::string> (response.data (omitBody));
  asio::async_write (
    _socket,
    asio::buffer (*data),
    [ctx = shared_from_this(), data, closeAfter] (std::error_code ec, size_t) {
      if (ec || closeAfter) {
        ctx->close();
        return;
      }
      ctx->_state = std::make_unique<RequestState> (ctx->_logger.get());
      ctx->waitForHttpMessage();
    }
  );
}

}
