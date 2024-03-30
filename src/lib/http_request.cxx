// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <exception>

#include <http_parser.h>
// #include "rapidjson/document.h"

#include <lightning/http_request.h>
//https://github.com/nodejs/http-parser


namespace lightning {

// ----------------------------------------------------------------------------
// ParserData
// ----------------------------------------------------------------------------
using ParserData = struct {
  HttpRequest *request;
  const char *pHeaderName;
  size_t headerNameSize;
};

// ----------------------------------------------------------------------------
// HttpRequest::parse
// ----------------------------------------------------------------------------
// bool HttpRequest::parse (const char *buffer, size_t bufLen) {
bool HttpRequest::parse (std::string_view buffer) {
  // std::ignore = buffer;
  // std::ignore = bufLen;
  http_parser_settings settings;
  http_parser *parser = new http_parser;
  ParserData data { this, nullptr, 0 };

  http_parser_init (parser, HTTP_REQUEST);
  parser->data = static_cast<void *> (&data);

  settings.on_message_begin = [] ([[maybe_unused]] http_parser *parser) -> int {
    return 0;
  };
  settings.on_url = [] (http_parser *parser, const char *at, size_t length) -> int {
    ParserData *data = static_cast<ParserData *>(parser->data);
    struct http_parser_url urlParser;

    data->request->url = std::string (at, length);

    http_parser_url_init (&urlParser);
    if (http_parser_parse_url (at, length, 0, &urlParser) == 0) {
      if ((urlParser.field_set & (1 << UF_PATH)) != 0)
        data->request->path = std::string (at + urlParser.field_data[UF_PATH].off, urlParser.field_data[UF_PATH].len);
      if ((urlParser.field_set & (1 << UF_QUERY)) != 0) {
        data->request->query = std::string (at + urlParser.field_data[UF_QUERY].off, urlParser.field_data[UF_QUERY].len);
      }
    }

    return 0;
  };
  settings.on_status = [] ([[maybe_unused]] http_parser *parser, [[maybe_unused]] const char *at, [[maybe_unused]] size_t length) -> int {
    return 0;
  };
  settings.on_header_field = [] (http_parser *parser, const char *at, size_t length) -> int {
    static_cast<ParserData *>(parser->data)->pHeaderName = at;
    static_cast<ParserData *>(parser->data)->headerNameSize = length;

    return 0;
  };
  settings.on_header_value = [] (http_parser *parser, const char *at, size_t length) -> int {
    ParserData *data = static_cast<ParserData *>(parser->data);

    std::string_view name (data->pHeaderName, data->headerNameSize);
    std::string_view value (at, length);

    data->request->headers.set (name, value);
//
//    if (name.compare ("Host") == 0) {
//      std::string::size_type pos = value.rfind (":");
//      data->request->host = value.substr (0, pos);
//      if (pos != std::string::npos) {
//        std::string_view port = value.substr (pos + 1);
//        data->request->port = std::atoi (port.data());
//      }
//      else {
//        data->request->port = 80;
//      }
//    }

    return 0;
  };
  settings.on_headers_complete = [] ([[maybe_unused]] http_parser *parser) -> int {
    return 0;
  };
  settings.on_body = [] (http_parser *parser, const char *at, size_t length) -> int {
    static_cast<HttpRequest *>(parser->data)->body.assign (length, reinterpret_cast<const uint8_t *>(at));
    return 0;
  };
  settings.on_message_complete = [] (http_parser *parser) -> int {
    ParserData *data = static_cast<ParserData *>(parser->data);
    switch (parser->method) {
      case HTTP_DELETE:
        data->request->method = HttpMethod::kDelete;
        break;
      case HTTP_GET:
        data->request->method = HttpMethod::kGet;
        break;
      case HTTP_HEAD:
        data->request->method = HttpMethod::kHead;
        break;
      case HTTP_POST:
        data->request->method = HttpMethod::kPost;
        break;
      case HTTP_PUT:
        data->request->method = HttpMethod::kPut;
        break;
      case HTTP_CONNECT:
        data->request->method = HttpMethod::kConnect;
        break;
      case HTTP_OPTIONS:
        data->request->method = HttpMethod::kOptions;
        break;
      case HTTP_PATCH:
        data->request->method = HttpMethod::kPatch;
        break;
      case HTTP_TRACE:
        data->request->method = HttpMethod::kTrace;
        break;
      default:
        throw std::exception ();
    }

    data->request->statusCode = parser->status_code;
    data->request->version.major = parser->http_major;
    data->request->version.minor = parser->http_minor;

    return 0;
  };
  settings.on_chunk_header = [] ([[maybe_unused]] http_parser *parser) -> int {
    return 0;
  };
  settings.on_chunk_complete = [] ([[maybe_unused]] http_parser *parser) -> int {
    return 0;
  };

  // http_parser_execute (parser, &settings, buffer, bufLen);
  http_parser_execute (parser, &settings, buffer.data(), buffer.size());

//  for (auto &funct: _parsers)
//    funct (*this);

  return true;
  return false;
}

// ----------------------------------------------------------------------------
// HttpRequest::queryParser
// ----------------------------------------------------------------------------
void HttpRequest::queryParser (HttpRequest &req) {
 if (!req.query.empty()) {
   // TODO
 }
}

// ----------------------------------------------------------------------------
// HttpRequest::bodyParser
// ----------------------------------------------------------------------------
void HttpRequest::bodyParser (HttpRequest &req) {
 if (!req.body.empty()) {
  //  if (req.headers.get ("content-type").compare (0, 16, "application/json") == 0) {
    // FIXME
    //  rapidjson::Document doc;
    //  doc.Parse (reinterpret_cast<char  *> (req.body.data()));
  //  }
 }
}

}
