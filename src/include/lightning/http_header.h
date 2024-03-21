// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_HEADER_H__
#define __LIGHTNING_HTTP_HEADER_H__
#include <unordered_map>
#include <string>
#include <string_view>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpHeader
// ----------------------------------------------------------------------------
class HttpHeader {
  public:
    struct HeaderData {
      std::string_view name;
      std::string_view value;
    };

    using iterator = std::unordered_map<std::string, HeaderData>::iterator;

    bool has (std::string_view name);
    std::string_view get (std::string_view name, std::string_view defaultValue="");
    void set (std::string_view name, std::string_view value);

    size_t size();

    std::string_view operator[] (std::string_view name);

    iterator begin () { return _headers.begin(); }
    iterator end () { return _headers.end(); }

  private:
    std::unordered_map<std::string, HeaderData> _headers;
};

}

#endif
