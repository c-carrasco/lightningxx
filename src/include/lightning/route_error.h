// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_ROUTE_ERROR_H
#define LIGHTNING_ROUTE_ERROR_H
#include <stdexcept>

namespace lightning {

// Raised when a matched route capture contains a malformed escape or a null byte.
class RouteDecodeError: public std::invalid_argument {
  public:
    RouteDecodeError(): std::invalid_argument { "Invalid encoding in route parameter" } {}
};

}
#endif
