// ----------------------------------------------------------------------------
// MIT License
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_URL_PARAMETERS_H
#define LIGHTNING_URL_PARAMETERS_H
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <lightning/request_parse_error.h>

namespace lightning {
// Flat names; repeated values retain their arrival order. No bracket expansion.
using UrlParameters = std::map<std::string, std::vector<std::string>>;
struct UrlEncodedOptions {
  size_t limit { 100 * 1024 };
  size_t parameterLimit { 1000 };
};
UrlParameters parseUrlEncoded (std::string_view input, UrlEncodedOptions options = {});
}
#endif
