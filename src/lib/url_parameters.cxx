// ----------------------------------------------------------------------------
// MIT License
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <lightning/url_parameters.h>


namespace lightning {

namespace {

// ----------------------------------------------------------------------------
// Percent-decoding utility
// ----------------------------------------------------------------------------
int hex (char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

// ----------------------------------------------------------------------------
// URL-decoding function
// ----------------------------------------------------------------------------
std::string decode (std::string_view input) {
  std::string result;
  result.reserve (input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%') {
      if (input.size() - i < 3 || hex (input[i + 1]) < 0 || hex (input[i + 2]) < 0)
        throw RequestParseError {};
      result += static_cast<char> ((hex (input[i + 1]) << 4) | hex (input[i + 2]));
      i += 2;
    }
    else result += input[i] == '+' ? ' ' : input[i];
  }

  return result;
}

} // unnamed namespace

// ----------------------------------------------------------------------------
// URL-encoded form parsing
// ----------------------------------------------------------------------------
UrlParameters parseUrlEncoded (std::string_view input, UrlEncodedOptions options) {
  if (input.size() > options.limit)
    throw RequestParseError { RequestParseError::Code::kTooLarge };

  UrlParameters result;
  size_t count = 0;
  while (!input.empty()) {
    const auto end = input.find ('&');
    const auto field = input.substr (0, end);
    if (!field.empty()) {
      if (count == options.parameterLimit)
        throw RequestParseError { RequestParseError::Code::kTooLarge };

      ++count;
      const auto equal = field.find ('=');
      auto name = decode (field.substr (0, equal));
      auto value = equal == std::string_view::npos ? std::string {} : decode (field.substr (equal + 1));
      result[std::move (name)].push_back (std::move (value));
    }

    if (end == std::string_view::npos) break;
    input.remove_prefix (end + 1);
  }

  return result;
}

}
