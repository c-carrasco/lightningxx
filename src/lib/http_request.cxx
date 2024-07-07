// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <cassert>
#include <cstring>
#include <exception>
#include <regex>

#include <llhttp.h>

#include <lightning/http_request.h>
#include <lightning/string_util.h>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpRequest::parse
// ----------------------------------------------------------------------------
bool HttpRequest::parse (std::string_view buffer) {
  llhttp_t parser;
  llhttp_settings_t settings;

  llhttp_settings_init (&settings);
  settings.on_method = [] (llhttp_t *parser, const char *at, size_t length) {
    auto request { static_cast<HttpRequest *> (parser->data) };
    assert (request);

    if (std::strncmp ("GET", at, length) == 0) request->method = HttpMethod::kGet;
    else if (std::strncmp ("HEAD", at, length) == 0) request->method = HttpMethod::kHead;
    else if (std::strncmp ("POST", at, length) == 0) request->method = HttpMethod::kPost;
    else if (std::strncmp ("PUT", at, length) == 0) request->method = HttpMethod::kPut;
    else if (std::strncmp ("DELETE", at, length) == 0) request->method = HttpMethod::kDelete;
    else if (std::strncmp ("CONNECT", at, length) == 0) request->method = HttpMethod::kConnect;
    else if (std::strncmp ("OPTIONS", at, length) == 0) request->method = HttpMethod::kOptions;
    else if (std::strncmp ("TRACE", at, length) == 0) request->method = HttpMethod::kTrace;
    else if (std::strncmp ("PATCH", at, length) == 0) request->method = HttpMethod::kPatch;
    else {
      request->_logger.get().error ("HTTP parsing error: unsupported HTTP method {}", std::string_view { at, length });

      return -1;
    }

    return 0;
  };

  settings.on_status = [] (llhttp_t *parser, const char *at, [[maybe_unused]] size_t length) {
    auto request { static_cast<HttpRequest *> (parser->data) };

    char *end { nullptr };
    request->statusCode = std::strtol (at, &end, 10);

    if (end == nullptr) {
      request->_logger.get().error ("Error parsing status code");

      return -1;
    }

    return 0;
  };

  settings.on_url = [] (llhttp_t* parser, const char *at, size_t length) {
    auto request { static_cast<HttpRequest *> (parser->data) };

    request->_logger.get().verbose ("HTTP URL parser: {}", std::string_view { at, length });

    request->url.assign (at, length);

    const std::regex pattern { "^([^?]*)(\\?([^#]*))?" };
    std::smatch parts;

    if (std::regex_search(request->url, parts, pattern)) {
      request->path = parts[1].str();
      request->query = parts[3].str(); // Part 3 is the query without the '?'
    }

    return 0;
  };

  settings.on_header_field = [] (llhttp_t *parser, const char *at, size_t length) {
    auto request { static_cast<HttpRequest *> (parser->data) };

    request->_logger.get().verbose ("HTTP header name parser: {}", std::string_view { at, length });

    request->headers.set (std::string_view { at, length}, std::string_view {}); // empty value

    return 0;
  };

  settings.on_header_value = [] (llhttp_t *parser, const char *at, size_t length) {
    auto request { static_cast<HttpRequest*>(parser->data) };

    const std::string_view value { at, length };

    request->_logger.get().verbose ("HTTP header value parser: {}", value);

    if (request->headers.last()->second.empty())
      request->headers.last()->second = value;
    else
      request->_logger.get().warn ("HTTP parsing warning: header '{}' already set", request->headers.last()->first);

    if (request->headers.last()->first.compare ("host") == 0) {
      if (const auto pos = value.find (':'); pos != std::string_view::npos)
        request->host.assign (at, pos);
      else
        request->host.assign (at, length);
    }

    return 0;
  };

  settings.on_version = [] (llhttp_t *parser, const char *at, size_t length) {
    auto request { static_cast<HttpRequest*>(parser->data) };

    request->_logger.get().verbose ("HTTP version parser: {}", std::string_view { at, length });

    const std::regex pattern { "^(\\d+)\\.(\\d+)$" };
    std::cmatch parts;

    if (std::regex_search(at, at + length, parts, pattern)) {
      request->version.major = std::atoi (parts[1].str().c_str());
      request->version.minor = std::atoi (parts[2].str().c_str());
    }
    else {
      request->_logger.get().error ("HTTP parsing error: unknown version {}", std::string_view { at, length });
    }

    return 0;
  };

  settings.on_body = [] (llhttp_t *parser, const char *at, size_t length) {
    auto request { static_cast<HttpRequest*>(parser->data) };

    request->_logger.get().verbose ("HTTP body parser: {}", StringUtil::fmtBuffer (at, length, 0, 16, 8));

    request->body.reserve (length);
    request->body.assign (length, reinterpret_cast<const uint8_t *>(at));

    return 0;
  };

  _logger.get().verbose ("HTTP parsing buffer: {}", StringUtil::fmtBuffer (buffer.data(), buffer.size(), 0, 16, 8));

  llhttp_init (&parser, HTTP_BOTH, &settings);
  parser.data = this;

  // Parse the HTTP data
  if (const llhttp_errno err = llhttp_execute (&parser, buffer.data(), buffer.size()); err != HPE_OK) {
    _logger.get().error ("HTTP parsing error: {}", llhttp_errno_name (err));
    return false;
  }

  return true;
}


// ----------------------------------------------------------------------------
// HttpRequest::queryParser
// ----------------------------------------------------------------------------
// void HttpRequest::queryParser (HttpRequest &req) {
//  if (!req.query.empty()) {
//    // TODO
//  }
// }

// // ----------------------------------------------------------------------------
// // HttpRequest::bodyParser
// // ----------------------------------------------------------------------------
// void HttpRequest::bodyParser (HttpRequest &req) {
//  if (!req.body.empty()) {
//   //  if (req.headers.get ("content-type").compare (0, 16, "application/json") == 0) {
//     // FIXME
//     //  rapidjson::Document doc;
//     //  doc.Parse (reinterpret_cast<char  *> (req.body.data()));
//   //  }
//  }
// }

}
