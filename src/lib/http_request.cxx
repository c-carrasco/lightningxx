// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include "http_request_parser.h"


namespace lightning::detail {

// ----------------------------------------------------------------------------
// HTTP request parser settings
// ----------------------------------------------------------------------------
const llhttp_settings_t HttpRequestParser::_settings = [] {
  llhttp_settings_t settings;
  llhttp_settings_init (&settings);

  settings.on_url = [] (llhttp_t *parser, const char *at, size_t length) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    auto &request = self._request;

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
      default: request.method = HttpMethod::kUnknown; break;
    }

    if (length > self._limits.targetBytes - self._request.url.size()) {
      self._errorStatus = 414;
      return -1;
    }

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
    auto &self = *static_cast<HttpRequestParser *>(parser->data);

    if (length > self._limits.headerBytes - self._fieldBytes) {
      self._errorStatus = 431;
      return -1;
    }

    self._fieldBytes += length;
    self._headerName.append (at, length);

    return 0;
  };

  settings.on_header_value = [] (llhttp_t *parser, const char *at, size_t length) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);

    if (length > self._limits.headerBytes - self._fieldBytes) {
      self._errorStatus = 431;
      return -1;
    }

    self._fieldBytes += length;
    self._headerValue.append (at, length);

    return 0;
  };

  settings.on_header_value_complete = [] (llhttp_t *parser) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);

    if (self._headerCount == self._limits.headerCount) {
      self._errorStatus = 431;
      return -1;
    }

    ++self._headerCount;
    auto &headers = self._headersComplete ? self._request.trailers : self._request.headers;
    std::string name = self._headerName;
    for (auto &c : name) {
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    }

    if (
      self._headersComplete && (
        (name == "content-length") ||
        (name == "transfer-encoding") ||
        (name == "host") ||
        (name == "connection") ||
        (name == "content-type") ||
        (name == "content-encoding") ||
        (name == "authorization") ||
        (name == "expect") ||
        (name == "trailer")
      )
    ) {
      return -1;
    }

    if (headers.contains (name)) {
      if ((name == "host") || (name == "content-length"))
        return -1;

      headers.set (name, std::string (*headers.get (name)) + (name == "cookie" ? "; " : ", ") + self._headerValue);
    }
    else {
      headers.set (name, self._headerValue);
    }

    self._headerName.clear();
    self._headerValue.clear();

    return 0;
  };

  settings.on_headers_complete = [] (llhttp_t *parser) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    auto &request = self._request;

    self._headersComplete = true;
    if ((parser->http_major != 1) || (parser->http_minor > 1)) {
      self._errorStatus = 505;
      return -1;
    }

    if (request.headers.contains ("expect")) {
      self._errorStatus = 417;
      return -1;
    }

    if (request.headers.contains ("content-length") && (parser->content_length > self._limits.bodyBytes)) {
      self._errorStatus = 413;
      return -1;
    }

    if (request.method == HttpMethod::kUnknown)
      return -1;

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
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    auto &body = self._request.body;

    if (length > self._limits.bodyBytes - body.size()) {
      self._errorStatus = 413;
      return -1;
    }

    const auto bytes = reinterpret_cast<const uint8_t *>(at);
    body.insert (body.end(), bytes, bytes + length);

    return 0;
  };

  settings.on_chunk_header = [] (llhttp_t *parser) {
    auto &self = *static_cast<HttpRequestParser *>(parser->data);
    if (parser->content_length > self._limits.bodyBytes - self._request.body.size()) {
      self._errorStatus = 413;
      return -1;
    }

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

// ----------------------------------------------------------------------------
// HTTP request parser construction
// ----------------------------------------------------------------------------
HttpRequestParser::HttpRequestParser (HttpRequest &request, RequestLimits limits): _request { request }, _limits { limits } {
  llhttp_init (&_parser, HTTP_REQUEST, &_settings);
  _parser.data = this;
}

// ----------------------------------------------------------------------------
// HTTP request parser consumption
// ----------------------------------------------------------------------------
HttpRequestParser::Result HttpRequestParser::consume (std::string_view input) {
  // Count raw initial header bytes before handing them to the allocating parser.
  // Stop at the delimiter so coalesced bodies and pipelined messages are excluded.
  if (!_headerWireComplete) {
    size_t inspected = 0;
    for (const auto c : input) {
      if (_headerBytes == _limits.headerBytes) {
        // Parse only the bounded prefix to retain the method for HEAD errors.
        (void) llhttp_execute (&_parser, input.data(), inspected);
        _errorStatus = 431;
        return { Status::kInvalid, input.size(), false, _errorStatus };
      }

      ++_headerBytes;
      ++inspected;

      // Require CRLF consistently; bare LF would bypass delimiter accounting.
      if (((c == '\n') && (_delimiter != 1 && _delimiter != 3)) || (((_delimiter == 1) || (_delimiter == 3)) && (c != '\n')))
        return { Status::kInvalid, input.size(), false, 400 };

      constexpr std::string_view delimiter { "\r\n\r\n" };

      _delimiter = c == delimiter[_delimiter] ? _delimiter + 1 : (c == '\r' ? 1 : 0);

      if (_delimiter == 4) {
        _headerWireComplete = true;
        break;
      }
    }
  }

  const auto error = llhttp_execute (&_parser, input.data(), input.size());

  if ((error == HPE_INVALID_VERSION) && ((_parser.http_major > 1) || (_parser.http_minor > 1)))
    _errorStatus = 505;

  if (_complete && ((error == HPE_PAUSED) || (error == HPE_PAUSED_UPGRADE))) {
    return {
      Status::kComplete,
      static_cast<size_t> (llhttp_get_error_pos (&_parser) - input.data()), _keepAlive
    };
  }

  return { error == HPE_OK ? Status::kIncomplete : Status::kInvalid, input.size(), false, _errorStatus };
}

} // namespace lightning::detail

namespace lightning {

// ----------------------------------------------------------------------------
// HTTP request parsing
// ----------------------------------------------------------------------------
bool HttpRequest::parse (std::string_view input, RequestLimits limits) {
  *this = HttpRequest { _logger };
  detail::HttpRequestParser parser { *this, limits };
  const auto result = parser.consume (input);
  return result.status == detail::HttpRequestParser::Status::kComplete && result.consumed == input.size();
}

}
