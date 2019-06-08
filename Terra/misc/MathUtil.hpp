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

//! 線形な音量値からdBへの変換
//! linearがマイナスの場合はlinearの絶対値が使用される。
//! 4.0 -> 約 +12dB
//! 2.0 -> 約 +6dB
//! 1.0 ->    0dB
//! 0.5 -> 約 -6dB
//! 0.25-> 約 -12dB
//! 0.0 -> 約 -inf.dB
inline
double LinearToDB(double linear)
{
    linear = std::fabs(linear);
    static double const dB_640 = 0.00000000000000000000000000000001;
    if(linear < dB_640) {
        return -640;
    } else {
        return 20.0 * log10(linear);
    }
}

//! dBから線形な音量値への変換
//! +12dB   -> 約4.0
//! +6dB    -> 約2.0
//! 0dB     -> 1.0
//! -6dB    -> 約0.5
//! -12dB   -> 約0.25
//! -inf.dB -> 0.0
inline
double DBToLinear(double dB)
{
    return pow(10.0, dB/20.0);
}

NS_HWM_END
