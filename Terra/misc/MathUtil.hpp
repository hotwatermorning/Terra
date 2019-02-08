#pragma once

#include <cmath>

NS_HWM_BEGIN

template<class Dest, class Src>
inline
Dest Round(Src src)
{
    return static_cast<Dest>(std::round(src));
}

NS_HWM_END
