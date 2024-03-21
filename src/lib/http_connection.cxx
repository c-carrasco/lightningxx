// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <sstream>
#include <algorithm>
#include <iterator>
#include <string>
#include <iostream>

#include <asio.hpp>

#include <lightning/http_connection.h>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpConnection::waitForHttpMessage
// ----------------------------------------------------------------------------
void HttpConnection::waitForHttpMessage() {
  if (_inputBuffer.length() != 0) {
    // If a pipeline requests were sent by client then the beginning (or even entire request) of it
    // is in the buffer obtained from socket in previous read operation.
    _consumeData (_inputBuffer.bytes(), _inputBuffer.length());
  }
  else {
    // Next request (if any) must be obtained from socket
    _consumeMessage();
  }
}

// ----------------------------------------------------------------------------
// HttpConnection::_consumeMessage
// ----------------------------------------------------------------------------
void HttpConnection::_consumeMessage() {
  _socket.async_read_some(
    _inputBuffer.makeAsioBuffer(),
    [ this, ctx = shared_from_this() ] (auto ec, std::size_t length) {
      this->_afterRead (ec, length);
    }
  );
}

// ----------------------------------------------------------------------------
// HttpConnection::_afterRead
// ----------------------------------------------------------------------------
void HttpConnection::_afterRead (const std::error_code &ec, std::size_t length) {
  if (!ec) {
    _inputBuffer.obtainedBytes (length);
    _consumeData (_inputBuffer.bytes(), length);
  }
  else {
    _socket.close();
  }
}

// ----------------------------------------------------------------------------
// HttpConnection::_consumeData
// ----------------------------------------------------------------------------
void HttpConnection::_consumeData (const char *data, std::size_t length) {
  bool messageComplete = false;
  const char *d = data;

  // Assume "\r\n\r\n" - the end of message will never happened to be split in 2 read operations.
  for (; d <= data + length - 4; ++d) {
    if (d[0] == '\r' && d[1] == '\n' && d[2] == '\r' && d[3] == '\n') {
      messageComplete = true;
      break;
    }
  }

  if (messageComplete) {
    HttpRequest request;
    HttpResponse response;

    _inputBuffer.consumedBytes (d - data + 4);

    _parseResponse (request);

    request.ip = _socket.remote_endpoint().address().to_string();
    request.protocol = ProtocolType::kHttp; // FIXME: support more protocols

    _onReceivedRequest (request, response);

    _writeResponseMessage (response);
  }
  else {
    _inputBuffer.consumedBytes (length);
    _consumeMessage();
  }
}

// ----------------------------------------------------------------------------
// HttpConnection::_parseResponse
// ----------------------------------------------------------------------------
void HttpConnection::_parseResponse (HttpRequest &request) {
  const char *buf = _inputBuffer.bytes();
  size_t s = strlen (buf);

  request.parse (buf, s);
}

// ----------------------------------------------------------------------------
// HttpConnection::_writeResponseMessage
// ----------------------------------------------------------------------------
void HttpConnection::_writeResponseMessage (HttpResponse &response) {
  std::ignore = response;
  // std::string data = response.data();

  // asio::async_write(
  //   _socket,
  //   asio::buffer (data.data(), data.size()),
  //   [ this, ctx = shared_from_this() ] (auto & ec, std::size_t) {
  //     if (ec) {
  //       if (ec != asio::error::operation_aborted)
  //         _socket.close();
  //     }
  //     else {
  //       ctx->waitForHttpMessage();
  //     }
  //   }
  // );
}

}

