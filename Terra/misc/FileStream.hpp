#pragma once

#include <fstream>
#include "./StrCnv.hpp"

NS_HWM_BEGIN

template<class... Args>
std::ifstream open_ifstream(String path, Args &&... args)
{
#if defined(_MSC_VER)
    return std::ifstream(path.c_str(), std::forward<Args>(args)...);
#else
    return std::ifstream(to_utf8(path), std::forward<Args>(args)...);
#endif
}

template<class... Args>
std::ofstream open_ofstream(String path, Args &&... args)
{
    #if defined(_MSC_VER)
        return std::ofstream(path.c_str(), std::forward<Args>(args)...);
    #else
        return std::ofstream(to_utf8(path), std::forward<Args>(args)...);
    #endif
}

template<class... Args>
std::fstream open_fstream(String path, Args &&... args)
{
    #if defined(_MSC_VER)
        return std::fstream(path.c_str(), std::forward<Args>(args)...);
    #else
        return std::fstream(to_utf8(path), std::forward<Args>(args)...);
    #endif
}

NS_HWM_END
