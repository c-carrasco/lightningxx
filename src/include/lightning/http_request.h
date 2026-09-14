// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_REQUEST_H__
#define __LIGHTNING_HTTP_REQUEST_H__
#include <cinttypes>
#include <any>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

#include <lightning/http_method.h>
#include <lightning/http_header.h>
#include <lightning/types.h>
#include <lightning/json.h>
#include <lightning/url_parameters.h>
#include <lightning/transport_options.h>


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
    // using ParseHandler = std::function<void (HttpRequest &)>;

    HttpRequest (std::reference_wrapper<const Logger> _logger): _logger { std::move (_logger) } {
      // empty
    }

    HttpMethod method { HttpMethod::kUnknown };
    std::string path;
    // Captures are owned, decoded once, and scoped to the current route/mount.
    std::unordered_map<std::string, std::string> params;
    // Raw mount prefix; path is relative to it while inside a router.
    std::string baseUrl;
    std::string query;
    std::string url;
    struct {
      uint16_t major;
      uint16_t minor;
    } version {};
    std::string host; // from headers (host)
    // uint16_t port;
    std::string ip;
    ProtocolType protocol { ProtocolType::kUnknown };
    HttpHeader headers;
    HttpHeader trailers;
    int32_t statusCode { 0 };
    std::vector<uint8_t> body;
    // Populated only by the corresponding body-parser middleware.
    std::optional<Json> jsonBody;
    std::optional<UrlParameters> formBody;
    // Per-request middleware context; values retain their C++ types via std::any.
    std::unordered_map<std::string, std::any> locals;

    // Parse exactly one complete request. All parsed data is owned by this object.
    bool parse (std::string_view data, RequestLimits limits = {});

    // Parse the current raw query on demand; the returned values own their data.
    UrlParameters queryParams (UrlEncodedOptions options = {}) const {
      return parseUrlEncoded (query, options);
    }

  private:
    // std::vector<ParseHandler> _parsers;
    std::reference_wrapper<const Logger> _logger;
};

}

#endif
