// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/http_header.h>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpHeaders::contains
// ----------------------------------------------------------------------------
bool HttpHeader::contains (std::string_view name) const {
  std::string lower;
  lower.resize(name.size());
  std::transform (std::begin (name), std::end (name), std::begin(lower), [] (unsigned char c) {
    return std::tolower (c);
  });

  return _headers.find (lower) != _headers.end();
}

// ----------------------------------------------------------------------------
// HttpHeader::get
// ----------------------------------------------------------------------------
std::optional<std::string_view> HttpHeader::get (std::string_view name) const {
  std::string lower;
  lower.resize(name.size());
  std::transform (std::begin (name), std::end (name), std::begin(lower), [] (unsigned char c) {
    return std::tolower (c);
  });

  if (auto it = _headers.find (lower); it != _headers.end())
    return { it->second.value };

  return std::nullopt;
}

// ----------------------------------------------------------------------------
// HttpHeader::set
// ----------------------------------------------------------------------------
void HttpHeader::set (std::string_view name, std::string_view value) {
  std::string lower;
  lower.resize(name.size());
  std::transform (std::begin (name), std::end (name), std::begin(lower), [] (unsigned char c) {
    return std::tolower (c);
  });

  _headers.insert (std::make_pair (std::move (lower), HeaderData { std::move (name), std::move (value) }));
}

}
