#pragma once

#include <cstdint>
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>

#if __has_include(<optional>) == false
    #include <experimental/optional>
    namespace std {
        template<class... Args>
        using optional = std::experimental::optional<Args...>;
        constexpr std::experimental::nullopt_t nullopt{0};
    }
#else
    #include <optional>
#endif

#define NS_HWM_BEGIN namespace hwm {
#define NS_HWM_END }

NS_HWM_BEGIN

using String = std::wstring;
using SampleCount = std::int64_t;

NS_HWM_END

#include "./DebuggerOutputStream.hpp"
