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
      uint32_t errorStatus { 400 };
    };

    explicit HttpRequestParser (HttpRequest &request, RequestLimits limits = {});
    HttpRequestParser (const HttpRequestParser &) = delete;
    HttpRequestParser & operator= (const HttpRequestParser &) = delete;
    Result consume (std::string_view input);
    bool headersComplete() const { return _headersComplete; }

  private:
    static const llhttp_settings_t _settings;
    llhttp_t _parser {};
    HttpRequest &_request;
    RequestLimits _limits;
    uint32_t _errorStatus { 400 };
    size_t _headerBytes { 0 };
    size_t _fieldBytes { 0 };
    size_t _headerCount { 0 };
    unsigned _delimiter { 0 };
    bool _headerWireComplete { false };
    bool _headersComplete { false };
    std::string _headerName;
    std::string _headerValue;
    bool _complete { false };
    bool _keepAlive { false };
};

}
#endif
