// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_CONNECTION_H__
#define __LIGHTNING_HTTP_CONNECTION_H__
#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <asio.hpp>

#include <lightning/types.h>
#include <lightning/http_request.h>
#include <lightning/http_response.h>
#include <lightning/transport_options.h>
#include <lightning/async.h>


namespace lightning {

class InputBuffer {
  public:
    auto makeAsioBuffer() {
      return asio::buffer (_buf.data(), _buf.size());
    }

    void obtainedBytes (size_t length) {
      if (length > _buf.size())
        throw std::out_of_range ("Input buffer capacity exceeded");
      _readyLength = length; // current bytes in buffer
      _readyPos = 0; // reset current pos
    }

    void consumedBytes (size_t length) {
      if (length > _readyLength)
        throw std::out_of_range ("Cannot consume unavailable bytes");
      _readyLength -= length; // decrement buffer length.
      _readyPos += length; // shift current pos.
    }

    inline size_t length() const { return _readyLength; }

    const char * bytes() const { return _buf.data() + _readyPos; }

    inline std::string_view str() const {
      return std::string_view (bytes(), _readyLength);
    }

  private:
    std::array<char, 4096> _buf {};
    size_t _readyPos { 0 };
    size_t _readyLength { 0 };
};


class HttpConnection: public std::enable_shared_from_this<HttpConnection> {
  public:
    HttpConnection (
      asio::ip::tcp::socket && socket,
      std::function<void (HttpRequest &, HttpResponse &)> receivedRequest,
      const Logger &logger,
      TransportOptions options = {},
      AsyncRequestHandler asyncReceivedRequest = {}
    );

    ~HttpConnection();

    void waitForHttpMessage();
    // Call on the connection strand, or after the server's I/O workers stop.
    void close();

  private:
    asio::ip::tcp::socket _socket;
    asio::steady_timer _timer;
    TransportOptions _options;
    enum class Phase { kHeader, kBody, kHandler, kWrite, kIdle };
    Phase _phase { Phase::kHeader };
    uint64_t _timerGeneration { 0 };
    bool _closed { false };
    std::function<void (HttpRequest &, HttpResponse &)> _onReceivedRequest;
    AsyncRequestHandler _onReceivedAsync;
    asio::cancellation_signal _handlerCancellation;
    InputBuffer _inputBuffer;
    std::reference_wrapper<const Logger> _logger;
    struct RequestState;
    std::unique_ptr<RequestState> _state;

    void _waitForMessage();
    void _deadline (Phase phase, std::chrono::milliseconds duration);
    void _cancelDeadline();

    void _consumeMessage();
    void _afterRead (const std::error_code & ec, size_t length);
    void _consumeData (const char *data, size_t length);
    Task<> _dispatchRequest (bool keepAlive, bool omitBody);
    void _writeResponseMessage (HttpResponse, bool closeAfter, bool omitBody = false);
};

}

#endif
