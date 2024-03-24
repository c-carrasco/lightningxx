// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_REQUEST_H__
#define __LIGHTNING_HTTP_REQUEST_H__
#include <cinttypes>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <lightning/http_method.h>
#include <lightning/http_header.h>


namespace lightning {

enum class ProtocolType: int {
  kUnknown = 0,
  kHttp = 1
//  kHttps = 2,
//  kWs = 3,
//  kWss = 4
};

class HttpRequest {
  public:
    using ParseHandler = std::function<void (HttpRequest &)>;

    HttpMethod method;
    std::string path;
    std::string query;
    std::string url;
    struct {
      uint16_t major;
      uint16_t minor;
    } version;
//    std::string host; // from headers (host)
//    uint16_t port; // from headers (host)
    std::string ip;
    ProtocolType protocol;
    HttpHeader headers;
    struct {
      std::map<std::string, std::string> path;
      std::map<std::string, std::string> query;
      std::map<std::string, std::string> body; // parsed
    } params;
    int32_t statusCode;
    std::vector<const uint8_t *> body;

    bool parse (const char *data, size_t len);

    void use (ParseHandler &&handler) { _parsers.push_back (handler); }

    static void queryParser (HttpRequest &req);
    static void bodyParser (HttpRequest &req);

  private:
    std::vector<ParseHandler> _parsers;
};

}

#endif
