#pragma once

#include <cmath>
#include <algorithm>

NS_HWM_BEGIN

template<class Dest, class Src>
inline
Dest Round(Src src)
{
    return static_cast<Dest>(std::round(src));
}

template <class T>
constexpr const T& Clamp(const T& v, const T& low, const T& high)
{
    return std::min<T>(std::max<T>(v, low), high);
}

template <class T, class Compare>
constexpr const T& Clamp(const T& v, const T& low, const T& high, Compare comp)
{
    return std::min<T>(std::max<T>(v, low, comp), high, comp);
}

NS_HWM_END
