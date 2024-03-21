// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_CONNECTION_H__
#define __LIGHTNING_HTTP_CONNECTION_H__
#include <array>
#include <functional>
#include <system_error>

#include <asio.hpp>

#include <lightning/http_request.h>
#include <lightning/http_response.h>


namespace lightning {

class InputBuffer {
  public:
    auto makeAsioBuffer() {
      return asio::buffer (_buf.data(), _buf.size());
    }

    void obtainedBytes (size_t length) {
      _readyLength = length; // current bytes in buffer
      _readyPos = 0; // reset current pos
    }

    void consumedBytes (size_t length) {
      _readyLength -= length; // decrement buffer length.
      _readyPos += length; // shift current pos.
    }

    size_t length() const { return _readyLength; }

    const char * bytes() const { return _buf.data(); }

  private:
    std::array<char, 4096> _buf;
    size_t _readyPos {0};
    size_t _readyLength {0};
};


class HttpConnection: public std::enable_shared_from_this<HttpConnection> {
  public:
    HttpConnection (asio::ip::tcp::socket && socket, std::function<void (HttpRequest &, HttpResponse &)> receivedRequest):
      _socket { std::move (socket) },
      _onReceivedRequest { receivedRequest }
    {
      // empty
    }

    void waitForHttpMessage();

  private:
    asio::ip::tcp::socket _socket;
    std::function<void (HttpRequest &, HttpResponse &)> _onReceivedRequest;
    InputBuffer _inputBuffer;

    void _consumeMessage();
    void _afterRead (const std::error_code & ec, size_t length);
    void _consumeData (const char *data, size_t length);
    void _writeResponseMessage (HttpResponse &);
    void _parseResponse (HttpRequest &);
};

}

#endif
