// ----------------------------------------------------------------------------
// MIT License
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_REQUEST_PARSE_ERROR_H
#define LIGHTNING_REQUEST_PARSE_ERROR_H
#include <cstdint>
#include <stdexcept>

namespace lightning {
class RequestParseError: public std::runtime_error {
  public:
    enum class Code: uint32_t { kMalformed = 400, kTooLarge = 413, kUnsupportedMediaType = 415 };
    explicit RequestParseError (Code code = Code::kMalformed):
      std::runtime_error { code == Code::kTooLarge ? "Payload too large" :
        code == Code::kUnsupportedMediaType ? "Unsupported media type" : "Bad request" }, _code { code } {}
    uint32_t status() const { return static_cast<uint32_t> (_code); }
  private:
    Code _code;
};
}
#endif
