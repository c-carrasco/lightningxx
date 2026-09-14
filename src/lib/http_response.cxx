// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------

#include <lightning/http_response.h>
#include "http_field_validation.h"


namespace lightning {

// ----------------------------------------------------------------------------
// HttpResponse::send
// ----------------------------------------------------------------------------
HttpResponse & HttpResponse::send (const std::string &data) {
  _requireOpen();
  if (!_headers.contains ("content-type"))
    _headers.set ("content-type", "text/plain; charset=utf-8");

  _data = data;
  _finished = true;

  return *this;
}

// ----------------------------------------------------------------------------
// HttpResponse::end
// ----------------------------------------------------------------------------
HttpResponse & HttpResponse::end() {
  _requireOpen();
  _finished = true;
  return *this;
}

// ----------------------------------------------------------------------------
// HttpResponse::json
// ----------------------------------------------------------------------------
HttpResponse & HttpResponse::json (const Json &value) {
  _requireOpen();

  // Serialize before changing response state; invalid UTF-8 may throw.
  auto serialized = value.dump();
  _headers.set ("content-type", "application/json; charset=utf-8");
  _data = std::move (serialized);
  _finished = true;
  return *this;
}

// ----------------------------------------------------------------------------
// HttpResponse::data
// ----------------------------------------------------------------------------
std::string HttpResponse::data (bool omitBody) const {
  std::string res;

  res.append ("HTTP/1.1 ");
  res.append (std::to_string (_status));
  res.append (" \r\n");

  for (auto it = _headers.cbegin(); it != _headers.cend(); it++) {
    detail::validateField (it->first, it->second);
    // This buffered serializer owns framing, regardless of application headers.
    if (it->first == "content-length" || it->first == "transfer-encoding" || it->first == "trailer") continue;
    res.append (it->first);
    res.append (": ");
    res.append (it->second);
    res.append ("\r\n");
  }

  if (_status != 204 && _status != 304) {
    res.append ("content-length: ");
    res.append (std::to_string (_status == 205 ? 0 : _data.size()));
    res.append ("\r\n");
  }

  if (!_headers.contains ("server")) res.append ("server: lightning\r\n");
  res.append ("\r\n");
  if (!omitBody && _status != 204 && _status != 205 && _status != 304)
    res.append (_data);

  return res;
}

}
