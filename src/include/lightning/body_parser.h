// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_BODY_PARSER_H
#define LIGHTNING_BODY_PARSER_H
#include <lightning/middleware.h>
#include <lightning/url_parameters.h>

namespace lightning {

struct JsonOptions {
  size_t limit { 100 * 1024 };
  size_t maxDepth { 128 };
};

// Parse matching media types only; raw body bytes remain untouched.
Middleware json (JsonOptions options = {});
Middleware urlencoded (UrlEncodedOptions options = {});

}

#endif
