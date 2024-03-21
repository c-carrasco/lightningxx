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
bool HttpHeader::has (std::string_view name) {
  std::ignore = name;
  // std::transform (std::begin (name), std::end (name), std::begin(name), [] (unsigned char c) {
  //   return std::tolower (c);
  // });

  // return _headers.find (name) != _headers.end();
  return false;
}

// ----------------------------------------------------------------------------
// HttpHeader::get
// ----------------------------------------------------------------------------
std::string_view HttpHeader::get (std::string_view name, std::string_view defaultValue) {
  std::ignore = name;
  std::ignore = defaultValue;
  // std::transform (std::begin (name), std::end (name), std::begin(name), [] (unsigned char c) {
  //   return std::tolower (c);
  // });
  // auto it = _headers.find (name);

  // if (it != _headers.end())
  //   return it->second.value;

  return defaultValue;
}

// ----------------------------------------------------------------------------
// HttpHeader::set
// ----------------------------------------------------------------------------
void HttpHeader::set (std::string_view name, std::string_view value) {
  std::ignore = name;
  std::ignore = value;
  // std::transform (std::begin (name), std::end (name), std::begin(name), [] (unsigned char c) {
  //   return std::tolower (c);
  // });
  // _headers.insert (std::make_pair (name, HeaderData { name, value }));
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
