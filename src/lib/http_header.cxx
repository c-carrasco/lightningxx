// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/http_header.h>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpHeaders::has
// ----------------------------------------------------------------------------
bool HttpHeader::has (std::string_view name) const {
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
std::string_view HttpHeader::get (std::string_view name, std::string_view defaultValue) {
  std::string lower;
  lower.resize(name.size());
  std::transform (std::begin (name), std::end (name), std::begin(lower), [] (unsigned char c) {
    return std::tolower (c);
  });

  if (auto it = _headers.find (lower); it != _headers.end())
    return it->second.value;

  return defaultValue;
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

  _headers.insert (std::make_pair (std::move(lower), HeaderData { name, value }));
}

// ----------------------------------------------------------------------------
// HttpHeader::size
// ----------------------------------------------------------------------------
size_t HttpHeader::size () {
  return _headers.size();
}

// ----------------------------------------------------------------------------
// HttpHeader::operator[]
// ----------------------------------------------------------------------------
std::string_view HttpHeader::operator[] (std::string_view name) {
  return get (name);
}

}
