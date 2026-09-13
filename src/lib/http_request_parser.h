// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
// Internal incremental parser shared by HttpRequest and HttpConnection.
#ifndef LIGHTNING_HTTP_REQUEST_PARSER_H
#define LIGHTNING_HTTP_REQUEST_PARSER_H
#include <llhttp.h>
#include <lightning/http_request.h>

namespace lightning::detail {

class HttpRequestParser {
  public:
    enum class Status { kIncomplete, kComplete, kInvalid };
    struct Result {
      Status status;
      size_t consumed;
      bool keepAlive;
    };

    explicit HttpRequestParser (HttpRequest &request);
    HttpRequestParser (const HttpRequestParser &) = delete;
    HttpRequestParser & operator= (const HttpRequestParser &) = delete;
    Result consume (std::string_view input);

  private:
    static const llhttp_settings_t _settings;
    llhttp_t _parser {};
    HttpRequest &_request;
    std::string _headerName;
    std::string _headerValue;
    bool _complete { false };
    bool _keepAlive { false };
};

}
#endif
