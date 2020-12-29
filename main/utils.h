#ifndef __UTILS_H__
#define __UTILS_H__

namespace Utils
{
  template <typename T, typename U> 
  inline auto max(T a, U b)
  {
    return (a > b) ? a : b;
  }

  template <typename T, typename U>
  inline auto min(T a, U b)
  {
    return (a < b) ? a : b;
  }
}

#endif