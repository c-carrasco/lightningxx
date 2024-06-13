// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_RESPONSE_H__
#define __LIGHTNING_HTTP_RESPONSE_H__
#include <cinttypes>
#include <string>

#include <lightning/http_header.h>


namespace lightning {

class HttpResponse {
  public:
    std::string data() const;

    HttpResponse & status (uint32_t status) {
      _status = status;
      return *this;
    }
    HttpResponse & send (const std::string &data);

    HttpHeader & headers() { return _headers; }

  private:
    uint32_t _status = 0;
    HttpHeader _headers;
    std::string _data;
};

}

#endif
