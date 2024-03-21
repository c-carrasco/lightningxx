// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_METHOD_H__
#define __LIGHTNING_HTTP_METHOD_H__
#include <cstdint>


namespace lightning {

enum class HttpMethod: int_fast8_t {
  kUnknown = 0,
  kGet = 1,
  kHead = 2,
  kPost = 3,
  kPut = 4,
  kDelete = 5,
  kConnect = 6,
  kOptions = 7,
  kTrace = 8,
  kPatch = 9
};

}

#endif
