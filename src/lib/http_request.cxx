// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include "http_request_parser.h"

namespace lightning::detail {

const llhttp_settings_t HttpRequestParser::_settings = [] {
  llhttp_settings_t settings;
  llhttp_settings_init (&settings);
  settings.on_url = [] (llhttp_t *parser, const char *at, size_t length) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    self._request.url.append (at, length);
    return 0;
  };
  settings.on_url_complete = [] (llhttp_t *parser) {
    auto &request = static_cast<HttpRequestParser *>(parser->data)->_request;
    const auto query = request.url.find ('?');
    request.path = request.url.substr (0, query);
    if (query != std::string::npos)
      request.query = request.url.substr (query + 1);
    return 0;
  };
  settings.on_header_field = [] (llhttp_t *parser, const char *at, size_t length) {
    static_cast<HttpRequestParser *>(parser->data)->_headerName.append (at, length);
    return 0;
  };
  settings.on_header_value = [] (llhttp_t *parser, const char *at, size_t length) {
    static_cast<HttpRequestParser *>(parser->data)->_headerValue.append (at, length);
    return 0;
  };
  settings.on_header_value_complete = [] (llhttp_t *parser) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    self._request.headers.set (self._headerName, self._headerValue);
    self._headerName.clear();
    self._headerValue.clear();
    return 0;
  };
  settings.on_headers_complete = [] (llhttp_t *parser) {
    auto &request = static_cast<HttpRequestParser *>(parser->data)->_request;
    switch (llhttp_get_method (parser)) {
      case HTTP_GET: request.method = HttpMethod::kGet; break;
      case HTTP_HEAD: request.method = HttpMethod::kHead; break;
      case HTTP_POST: request.method = HttpMethod::kPost; break;
      case HTTP_PUT: request.method = HttpMethod::kPut; break;
      case HTTP_DELETE: request.method = HttpMethod::kDelete; break;
      case HTTP_CONNECT: request.method = HttpMethod::kConnect; break;
      case HTTP_OPTIONS: request.method = HttpMethod::kOptions; break;
      case HTTP_TRACE: request.method = HttpMethod::kTrace; break;
      case HTTP_PATCH: request.method = HttpMethod::kPatch; break;
      default: return -1;
    }
    request.version.major = llhttp_get_http_major (parser);
    request.version.minor = llhttp_get_http_minor (parser);
    if (const auto host = request.headers.get ("host")) {
      if (!host->empty() && host->front() == '[') {
        const auto end = host->find (']');
        if (end == std::string_view::npos)
          return -1;
        request.host = host->substr (0, end + 1);
      }
      else {
        request.host = host->substr (0, host->find (':'));
      }
    }
    return 0;
  };
  settings.on_body = [] (llhttp_t *parser, const char *at, size_t length) {
    auto &body = static_cast<HttpRequestParser *>(parser->data)->_request.body;
    const auto bytes = reinterpret_cast<const uint8_t *>(at);
    body.insert (body.end(), bytes, bytes + length);
    return 0;
  };
  settings.on_message_complete = [] (llhttp_t *parser) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    self._complete = true;
    self._keepAlive = llhttp_should_keep_alive (parser) && !llhttp_get_upgrade (parser);
    // Stop at this message's boundary, preserving any pipelined bytes.
    return static_cast<int> (HPE_PAUSED);
  };
  return settings;
}();

HttpRequestParser::HttpRequestParser (HttpRequest &request): _request { request } {
  llhttp_init (&_parser, HTTP_REQUEST, &_settings);
  _parser.data = this;
}

HttpRequestParser::Result HttpRequestParser::consume (std::string_view input) {
  const auto error = llhttp_execute (&_parser, input.data(), input.size());
  if (_complete && (error == HPE_PAUSED || error == HPE_PAUSED_UPGRADE)) {
    return { Status::kComplete,
      static_cast<size_t> (llhttp_get_error_pos (&_parser) - input.data()), _keepAlive };
  }
  return { error == HPE_OK ? Status::kIncomplete : Status::kInvalid, input.size(), false };
}

}

namespace lightning {

bool HttpRequest::parse (std::string_view input) {
  *this = HttpRequest { _logger };
  detail::HttpRequestParser parser { *this };
  const auto result = parser.consume (input);
  return result.status == detail::HttpRequestParser::Status::kComplete && result.consumed == input.size();
}

}
