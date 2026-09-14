// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_HTTP_FIELD_VALIDATION_H
#define LIGHTNING_HTTP_FIELD_VALIDATION_H
#include <stdexcept>
#include <string_view>


namespace lightning::detail {

// ----------------------------------------------------------------------------
// HTTP field validation
// ----------------------------------------------------------------------------
inline void validateField (std::string_view name, std::string_view value) {
  if (name.empty())
    throw std::invalid_argument ("Empty HTTP header name");

  for (const unsigned char c : name) {
    if (!(
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      (std::string_view { "!#$%&'*+-.^_`|~" }.find (static_cast<char> (c)) != std::string_view::npos)
    )) {
      throw std::invalid_argument ("Invalid HTTP header name");
    }
  }

  for (const unsigned char c : value) {
    if (((c < 32) && (c != '\t')) || (c == 127))
      throw std::invalid_argument ("Invalid HTTP header value");
  }
}

}
#endif
