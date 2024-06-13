// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#ifndef __LIGHTNING_STRING_UTIL_H__
#define __LIGHTNING_STRING_UTIL_H__
#include <cinttypes>
#include <iomanip>
#include <string>
#include <sstream>


namespace lightning {

class StringUtil {
  public:
    /// @brief format a float32_t array
    ///
    /// @param buffer Pointer to the float32_t array to be formatted
    /// @param len Number of elements in the array
    /// @param numStart Print the first "numStart" elements of the array
    /// @param numEnd Print the last "numEnd" elements of the array
    ///
    /// @return A string object
    template<typename T=float>
    static std::string fmtBuffer (
      const T *buffer,
      const int64_t len,
      const int32_t precision = 4,
      const int64_t numStart = 8,
      const int64_t numEnd = 4
    ) {
      typedef typename std::conditional<sizeof (T) == 1, uint32_t, T>::type CastType;

      std::stringstream ss;

      ss << std::fixed << std::setprecision (precision);

      if ((len == 0) || ((numStart == 0) && (numEnd == 0)))
        return "";

      for (int64_t i { 0 }; (i < numStart) && (i < len); ++i) {
        if (i != 0) ss << ", ";
        ss << static_cast<CastType> (buffer[i]);
      }

      if (const auto remaining { len - numStart - numEnd }; remaining > 0) {
        if (numStart != 0) ss << ", ";
        ss << "... (+" << remaining << " elements) ...";
      }

      const auto diff { len - numEnd };
      for (int64_t i = std::max (diff, numStart); i < len; ++i) {
        if (i != 0) ss << ", ";
        ss << static_cast<CastType> (buffer[i]);
      }

      return ss.str();
    }
};

}

#endif
