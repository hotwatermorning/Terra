#pragma once

#include <cstdint>
#include <wx/setup.h>
#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/tglbtn.h>

#include <fmt/format.h>

using namespace fmt::literals;

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/hosting/eventlist.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>

#if defined(_MSC_VER)

#include <optional>

#else

#include <experimental/optional>
namespace std {
    template<class... Args>
    using optional = std::experimental::optional<Args...>;
    constexpr std::experimental::nullopt_t nullopt{0};
}

#endif

#if __has_include(<variant>)
    #include <variant>

namespace hwm {
	template<class U, class... Args>
	U * get_if(std::variant<Args...> *v) { return std::get_if<U>(v); }
}

#else
    #include <mpark/variant.hpp>
    namespace std {
        template<class... Args>
        using variant = mpark::variant<Args...>;
        using monostate = mpark::monostate;
        using mpark::get;
        using mpark::get_if;
    }

	namespace hwm {
		template<class U, class... Args>
		U * get_if(std::variant<Args...> *v) { return mpark::get_if<U>(v); }
	}

#endif

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

#include "misc/DebuggerOutputStream.hpp"
