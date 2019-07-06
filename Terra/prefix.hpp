#pragma once

#include <cstdint>
#include <wx/setup.h>
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/tglbtn.h>
#include <wx/dcgraph.h>
#include <wx/filename.h>

#include <fmt/format.h>

#if defined(_MSC_VER)
namespace Gdiplus {
    using std::min;
    using std::max;
}

#include <gdiplus.h>
#endif

using namespace fmt::literals;

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>

#if __has_include(<optional>)

#include <optional>

#elif __has_include(<experimental/optional>)

#include <experimental/optional>
namespace std {
    template<class... Args>
    using optional = std::experimental::optional<Args...>;
    constexpr std::experimental::nullopt_t nullopt{0};
}

#else

#error Terra needs std::optional but not found.

#endif

#include <mpark/variant.hpp>

#define NS_HWM_BEGIN namespace hwm {
#define NS_HWM_END }

NS_HWM_BEGIN

using String = std::wstring;

using SampleCount = std::int64_t;
using AudioSample = float;
using Tick = std::int64_t;

using Int8 = std::int8_t;
using Int16 = std::int16_t;
using Int32 = std::int32_t;
using Int64 = std::int64_t;

using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;

constexpr wchar_t const *kAppName = L"Terra";

NS_HWM_END

#include "misc/StrCnv.hpp"
#include "misc/DebuggerOutputStream.hpp"
#include "log/LoggingSupport.hpp"
