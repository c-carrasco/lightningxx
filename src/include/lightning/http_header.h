// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_HTTP_HEADER_H__
#define __LIGHTNING_HTTP_HEADER_H__
#include <iostream>
#include <unordered_map>
#include <optional>
#include <string>
#include <string_view>


namespace lightning {

// ----------------------------------------------------------------------------
// HttpHeader
// ----------------------------------------------------------------------------
class HttpHeader {
  public:
    // struct HeaderData {
    //   std::string_view name;
    //   std::string_view value;
    // };

    using iterator = std::unordered_map<std::string, std::string_view>::iterator;
    using const_iterator = std::unordered_map<std::string, std::string_view>::const_iterator;

    bool contains (std::string_view name) const;

    std::optional<std::string_view> get (std::string_view name) const;

    void set (std::string_view name, std::string_view value);

    inline size_t size() const { return _headers.size(); }

    inline iterator begin () { return _headers.begin(); }
    inline iterator end () { return _headers.end(); }

    inline const_iterator cbegin () const { return _headers.begin(); }
    inline const_iterator cend () const { return _headers.end(); }

    inline iterator last() const { return _last; }

    friend std::ostream & operator<< (std::ostream &os, const HttpHeader &obj) {
      for (const auto &kv: obj._headers)
        os << kv.first << ": " << kv.second << std::endl;

      return os;
    }

  private:
    std::unordered_map<std::string, std::string_view> _headers;
    iterator _last;
};

}

#endif
