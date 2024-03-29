// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_HEADER_H__
#define __LIGHTNING_HTTP_HEADER_H__
#include <string>
#include <string_view>
#include <unordered_map>


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
    using const_iterator = std::unordered_map<std::string, HeaderData>::const_iterator;

    bool has (std::string_view name) const;
    std::string_view get (std::string_view name, std::string_view defaultValue="");
    void set (std::string_view name, std::string_view value);

    size_t size();

    std::string_view operator[] (std::string_view name);

    iterator begin () { return _headers.begin(); }
    iterator end () { return _headers.end(); }

    const_iterator cbegin () const { return _headers.begin(); }
    const_iterator cend () const { return _headers.end(); }

  private:
    // struct StringHash {
    //   using hash_type = std::hash<std::string_view>;
    //   using is_transparent = void;

    //   std::size_t operator() (const char *str) const        { return hash_type{} (str); }
    //   std::size_t operator() (std::string_view str) const   { return hash_type{} (str); }
    //   std::size_t operator() (const std::string &str) const { return hash_type{} (str); }
    // };

    std::unordered_map<std::string, HeaderData> _headers; // , StringHash, std::equal_to<>> _headers;
};

}

#endif
