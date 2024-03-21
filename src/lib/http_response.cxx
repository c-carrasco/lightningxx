// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
// #include "rapidjson/stringbuffer.h"
// #include "rapidjson/writer.h"

#include <lightning/http_response.h>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpResponse::send
// ----------------------------------------------------------------------------
HttpResponse & HttpResponse::send (const std::string &data) {
  if (!_headers.has ("Content-Type"))
    _headers.set ("Content-Type", "text/plain; charset=utf-8");

  _data = data;

  return *this;
}

// // ----------------------------------------------------------------------------
// // HttpResponse::send
// // ----------------------------------------------------------------------------
// HttpResponse & HttpResponse::send (const rapidjson::Document &document) {
//   if (!_headers.has ("Content-Type"))
//     _headers.set ("Content-Type", "application/json");

//   rapidjson::StringBuffer buffer;
//   rapidjson::Writer<rapidjson::StringBuffer> writer (buffer);
//   document.Accept (writer);
//   _data.assign (buffer.GetString());

//   return *this;
// }

// ----------------------------------------------------------------------------
// HttpResponse::data
// ----------------------------------------------------------------------------
std::string HttpResponse::data() {
  std::string res;

  res.append ("HTTP/1.1 ");
  res.append (std::to_string (_status));
  res.append ("\r\n");

  for (auto it = _headers.begin(); it != _headers.end(); it++) {
    res.append (it->second.name);
    res.append (": ");
    res.append (it->second.value);
    res.append ("\r\n");
  }

  if (!_headers.has ("Content-Length")) {
    res.append ("Content-Length: ");
    res.append (std::to_string (_data.size()));
    res.append ("\r\n");
  }

  res.append ("Server: ligthning");
  res.append ("\r\n");
  res.append ("\r\n");
  res.append (_data);

  return res;
}

}
