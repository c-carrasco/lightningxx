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
    // HttpResponse & send (const rapidjson::Document &document);

    HttpHeader & headers() { return _headers; }

  private:
    uint32_t _status = 0;
    HttpHeader _headers;
    std::string _data;

    // std::string _raw {
    //   "HTTP/1.1 200 OK\r\n"
    //   "Connection: keep-alive\r\n"
    //   "Content-Length: 12\r\n"
    //   "Server: Pure asio bench server\r\n"
    //   "Content-Type: text/plain; charset=utf-8\r\n"
    //   "\r\n"
    //   "Hello world!"
    // };
};

}

#endif
