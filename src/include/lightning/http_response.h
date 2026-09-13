// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_RESPONSE_H__
#define __LIGHTNING_HTTP_RESPONSE_H__
#include <cinttypes>
#include <string>
#include <stdexcept>

#include <lightning/http_header.h>
#include <lightning/json.h>


namespace lightning {
namespace detail { class Dispatch; }

class HttpResponse {
  public:
    std::string data (bool omitBody = false) const;

    HttpResponse & status (uint32_t status) {
      _requireOpen();
      if (status < 200 || status > 599)
        throw std::invalid_argument ("A buffered response needs a final HTTP status (200-599)");
      _status = status;
      return *this;
    }
    HttpResponse & send (const std::string &data);
    HttpResponse & json (const Json &value);
    HttpResponse & end();
    uint32_t status() const { return _status; }
    bool finished() const { return _finished; }
    bool shouldClose() const { return _closeAfter; }

    HttpHeader & headers() { return _headers; }
    const HttpHeader & headers() const { return _headers; }

  private:
    uint32_t _status = 200;
    HttpHeader _headers;
    std::string _data;
    bool _finished { false };
    bool _closeAfter { false };

    void _requireOpen() const {
      if (_finished)
        throw std::logic_error ("The response is already finished");
    }
    friend class detail::Dispatch;
};

}

#endif
