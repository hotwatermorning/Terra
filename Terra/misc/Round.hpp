#pragma once

NS_HWM_BEGIN

template<class Dest, class Src>
inline
Dest Round(Src src)
{
    return (Dest)std::round(src);
}

NS_HWM_END
