// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef LIGHTNING_ROUTE_PATTERN_H
#define LIGHTNING_ROUTE_PATTERN_H
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lightning::detail {

class RoutePattern {
  public:
    struct Match {
      size_t consumed;
      std::unordered_map<std::string, std::string> params;
    };
    RoutePattern (std::string_view path, bool prefix);
    std::optional<Match> match (std::string_view path) const;

  private:
    enum class Kind { kLiteral, kParameter, kWildcard };
    struct Segment { Kind kind; std::string value; };
    std::vector<Segment> _segments;
    bool _prefix;
    bool _asterisk { false };
};

}
#endif
