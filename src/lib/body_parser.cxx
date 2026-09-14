// ----------------------------------------------------------------------------
// MIT License
// Copyright (c) 2025 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <algorithm>
#include <lightning/body_parser.h>


namespace lightning {

namespace {

using Code = RequestParseError::Code;

// ----------------------------------------------------------------------------
// Trim whitespace from both ends of a string view
// ----------------------------------------------------------------------------
std::string_view trim (std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix (1);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix (1);
  return value;
}

// ----------------------------------------------------------------------------
// Convert a string view to lowercase
// ----------------------------------------------------------------------------
std::string lower (std::string_view value) {
  std::string result { value };
  for (auto &c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return result;
}

// ----------------------------------------------------------------------------
// Check if a character is a valid token character
// ----------------------------------------------------------------------------
bool token (char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
    std::string_view { "!#$%&'*+-.^_`|~" }.find (c) != std::string_view::npos;
}

// ----------------------------------------------------------------------------
// Validate parameters only after matching the media type. Unknown parameters
// are accepted, but charsets other than UTF-8 require an unsupported decoder.
// ----------------------------------------------------------------------------
void parameters (std::string_view input) {
  bool charset = false;

  while (!input.empty()) {
    input.remove_prefix (1); // semicolon
    input = trim (input);

    size_t length = 0;
    while (length < input.size() && token (input[length]))
      ++length;

    if (length == 0)
      throw RequestParseError {};

    const auto name = lower (input.substr (0, length));

    input = trim (input.substr (length));
    if (input.empty() || input.front() != '=')
      throw RequestParseError {};
    input = trim (input.substr (1));

    std::string value;
    if (!input.empty() && input.front() == '"') {
      input.remove_prefix (1);
      bool closed = false;
      while (!input.empty()) {
        auto c = input.front();
        input.remove_prefix (1);

        if (c == '"') {
            closed = true;
            break;
        }

        if (c == '\\') {
          if (input.empty()) throw RequestParseError {};
          c = input.front();
          input.remove_prefix (1);
        }

        if ((static_cast<unsigned char> (c) < 32 && c != '\t') || c == 127)
          throw RequestParseError {};

        value += c;
      }
      if (!closed)
        throw RequestParseError {};
    }
    else {
      length = 0;

      while (length < input.size() && token (input[length]))
        ++length;

      if (length == 0)
        throw RequestParseError {};

      value = input.substr (0, length);
      input.remove_prefix (length);
    }

    input = trim (input);
    if (!input.empty() && input.front() != ';')
      throw RequestParseError {};

    if (name == "charset") {
      if (charset)
        throw RequestParseError {};

      charset = true;

      if (lower (value) != "utf-8")
        throw RequestParseError { Code::kUnsupportedMediaType };
    }
  }
}

// ----------------------------------------------------------------------------
// URL-encoded body parser middleware
// ----------------------------------------------------------------------------
bool matches (const HttpRequest &request, bool isJson, size_t limit) {
  const auto header = request.headers.get ("content-type");
  if (!header)
    return false;

  const auto separator = header->find (';');
  const auto type = lower (trim (header->substr (0, separator)));
  const bool matching = isJson ?
    ((type == "application/json") || (type.starts_with ("application/") && type.ends_with ("+json") && (type.size() > 17) && std::all_of (type.begin() + 12, type.end(), token))) :
    type == "application/x-www-form-urlencoded";

  if (!matching)
    return false;

  if (request.body.size() > limit)
    throw RequestParseError { Code::kTooLarge };

  if (separator != std::string_view::npos)
    parameters (header->substr (separator));

  if (
    const auto encoding = request.headers.get ("content-encoding");
    encoding && (lower (trim (*encoding)) != "identity")
  ) {
    throw RequestParseError { Code::kUnsupportedMediaType };
  }

  return true;
}

} // unnamed namespace

// ----------------------------------------------------------------------------
// JSON body parser middleware
// ----------------------------------------------------------------------------
Middleware json (JsonOptions options) {
  if (options.maxDepth == 0)
    throw std::invalid_argument ("JSON maxDepth must be positive");

  return [options] (HttpRequest &request, HttpResponse &, Next next) {
    request.jsonBody.reset();

    if (matches (request, true, options.limit) && !request.body.empty()) {
      if (std::find (request.body.begin(), request.body.end(), uint8_t { 0 }) != request.body.end())
        throw RequestParseError {};

      try {
        request.jsonBody = Json::parse (request.body.begin(), request.body.end(),
          [options] (int depth, Json::parse_event_t event, Json &) {
            if (
              ((event == Json::parse_event_t::object_start) || (event == Json::parse_event_t::array_start)) &&
              (static_cast<size_t> (depth) >= options.maxDepth)
            ) {
              throw RequestParseError { Code::kTooLarge };
            }

            return true;
          });
      }
      catch (const Json::parse_error &) {
        throw RequestParseError {};
      }
      catch (const Json::out_of_range &) {
        throw RequestParseError {};
      }
    }

    next();
  };
}

// ----------------------------------------------------------------------------
// URL-encoded body parser middleware
// ----------------------------------------------------------------------------
Middleware urlencoded (UrlEncodedOptions options) {
  return [options] (HttpRequest &request, HttpResponse &, Next next) {
    request.formBody.reset();

    if (matches (request, false, options.limit)) {
      const std::string input { request.body.begin(), request.body.end() };
      request.formBody = parseUrlEncoded (input, options);
    }

    next();
  };
}

}
