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
  kGet = 0,
  kHead = 1,
  kPost = 2,
  kPut = 3,
  kDelete = 4,
  kConnect = 5,
  kOptions = 6,
  kTrace = 7,
  kPatch = 8
};

constexpr int_fast8_t kNumHttpMethods { static_cast<int_fast8_t> (HttpMethod::kPatch) + 1 };

}

#endif
