// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <unordered_set>
#include <lightning/route_error.h>
#include "route_pattern.h"

namespace lightning::detail {

namespace {

// ----------------------------------------------------------------------------
// Route pattern utility functions
// ----------------------------------------------------------------------------
bool nameStart (char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// ----------------------------------------------------------------------------
// Hexadecimal utility
// ----------------------------------------------------------------------------
int hex (char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// ----------------------------------------------------------------------------
// Percent-decoding utility
// ----------------------------------------------------------------------------
std::string decode (std::string_view value) {
  std::string decoded;
  decoded.reserve (value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if (c == '%') {
      if (i + 2 >= value.size() || hex (value[i + 1]) < 0 || hex (value[i + 2]) < 0)
        throw RouteDecodeError {};
      c = static_cast<char> (hex (value[i + 1]) * 16 + hex (value[i + 2]));
      i += 2;
    }
    if (c == '\0') throw RouteDecodeError {};
    decoded.push_back (c);
  }
  return decoded;
}

} // namespace lightning::detail

// ----------------------------------------------------------------------------
// Route pattern construction
// ----------------------------------------------------------------------------
RoutePattern::RoutePattern (std::string_view path, bool prefix): _prefix { prefix } {
  if (!prefix && path == "*") { _asterisk = true; return; }
  if (path.empty() || path.front() != '/' || path.find_first_of ("?#{}[]()") != std::string_view::npos ||
      path.find ('\0') != std::string_view::npos)
    throw std::invalid_argument ("Route patterns must be absolute paths without queries or optional/regex syntax");
  if (prefix) {
    while (path.size() > 1 && path.back() == '/') path.remove_suffix (1);
    if (path == "/") return;
  }
  std::unordered_set<std::string> names;
  size_t start = 1;
  while (start <= path.size()) {
    const auto slash = path.find ('/', start);
    const auto end = slash == std::string_view::npos ? path.size() : slash;
    const auto part = path.substr (start, end - start);
    Segment segment { Kind::kLiteral, std::string (part) };
    if (!part.empty() && (part.front() == ':' || part.front() == '*')) {
      const auto name = part.substr (1);
      if (name.empty() || !nameStart (name.front()))
        throw std::invalid_argument ("Route parameters need an identifier name");
      for (const char c : name)
        if (!nameStart (c) && !(c >= '0' && c <= '9'))
          throw std::invalid_argument ("Route parameters must occupy an entire path segment");
      segment.kind = part.front() == ':' ? Kind::kParameter : Kind::kWildcard;
      segment.value = name;
      if (!names.insert (segment.value).second)
        throw std::invalid_argument ("Duplicate route parameter name");
      if (segment.kind == Kind::kWildcard && (prefix || slash != std::string_view::npos))
        throw std::invalid_argument ("Wildcards are supported only as the final segment of a route");
    }
    else if (part.find_first_of (":*") != std::string_view::npos) {
      throw std::invalid_argument ("Route parameters must occupy an entire path segment");
    }
    _segments.push_back (std::move (segment));
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
}

// ----------------------------------------------------------------------------
// Route pattern matching
// ----------------------------------------------------------------------------
std::optional<RoutePattern::Match> RoutePattern::match (std::string_view path) const {
  if (_asterisk)
    return path == "*" ? std::optional<Match> { Match { 1, {} } } : std::nullopt;
  if (_prefix && _segments.empty()) return Match { 0, {} };
  if (path.empty() || path.front() != '/') return std::nullopt;
  // Match raw segments first. Decode only after the entire pattern matches, so
  // an unrelated route cannot reject a request because of one partial capture.
  std::vector<std::pair<std::string_view, std::string_view>> captures;
  size_t start = 1;
  size_t consumed = 0;
  for (size_t i = 0; i < _segments.size(); ++i) {
    if (start > path.size()) return std::nullopt;
    const auto &segment = _segments[i];
    const auto slash = path.find ('/', start);
    const auto end = segment.kind == Kind::kWildcard || slash == std::string_view::npos ? path.size() : slash;
    const auto value = path.substr (start, end - start);
    if (segment.kind == Kind::kLiteral) {
      if (value != segment.value) return std::nullopt;
    }
    else {
      if (value.empty()) return std::nullopt;
      captures.emplace_back (segment.value, value);
    }
    consumed = end;
    start = end + 1;
  }
  if (!_prefix && consumed != path.size()) return std::nullopt;
  Match result { consumed, {} };
  for (const auto &[name, value] : captures)
    result.params.emplace (name, decode (value));
  return result;
}

}
