#pragma once

#include <array>
#include <memory>

#include <Windows.h>

#include <boost/function.hpp>
#include <balor/String.hpp>
#include <boost/optional.hpp>

#include "vst3/pluginterfaces/gui/iplugview.h"
#include "vst3/pluginterfaces/base/ipluginbase.h"
#include "vst3/pluginterfaces/vst/ivstcomponent.h"
#include "vst3/pluginterfaces/vst/ivsteditcontroller.h"
#include "vst3/public.sdk/source/vst/hosting/parameterchanges.h"
#include "./Vst3Utils.hpp"

namespace hwm {

class Vst3Plugin;

struct FactoryInfo
{
public:
	bool discardable				() const;
	bool license_check				() const;
	bool component_non_discardable	() const;
	bool unicode					() const;

	balor::String	vendor	() const;
	balor::String	url		() const;
	balor::String	email	() const;

public:
	FactoryInfo() {}
	FactoryInfo(Steinberg::PFactoryInfo const &info);

private:
	balor::String vendor_;
	balor::String url_;
	balor::String email_;
	Steinberg::int32 flags_;
};

struct ClassInfo2Data
{
	ClassInfo2Data(Steinberg::PClassInfo2 const &info);
	ClassInfo2Data(Steinberg::PClassInfoW const &info);

	balor::String const &	sub_categories() const { return sub_categories_; }
	balor::String const &	vendor() const { return vendor_;; }
	balor::String const &	version() const { return version_; }
	balor::String const &	sdk_version() const { return sdk_version_; }

private:
	balor::String	sub_categories_;
	balor::String	vendor_;
	balor::String	version_;
	balor::String	sdk_version_;
};

struct ClassInfo
{
public:
	ClassInfo(Steinberg::PClassInfo const &info);
	ClassInfo(Steinberg::PClassInfo2 const &info);
	ClassInfo(Steinberg::PClassInfoW const &info);

	Steinberg::int8	const *	cid() const { return cid_.data(); }
	balor::String const &	name() const { return name_; }
	balor::String const &	category() const { return category_; }
	Steinberg::int32        cardinality() const { return cardinality_; }

	bool is_classinfo2_enabled() const { return static_cast<bool>(classinfo2_data_); }
	ClassInfo2Data const &
			classinfo2() const { return *classinfo2_data_; }

private:
	std::array<Steinberg::int8, 16> cid_;
	balor::String		name_;
	balor::String		category_;
	Steinberg::int32	cardinality_;
	boost::optional<ClassInfo2Data> classinfo2_data_;
};

class Vst3PluginFactory
{
public:
	Vst3PluginFactory(balor::String module_path);
	~Vst3PluginFactory();

	FactoryInfo const &
			GetFactoryInfo() const;

	//! PClassInfo::category ‚ª kVstAudioEffectClass ‚Ì‚à‚Ì‚Ì‚Ý
	size_t GetComponentCount() const;

	ClassInfo const &
			GetComponentInfo(size_t index);

	typedef std::unique_ptr<Steinberg::FUnknown, SelfReleaser> host_context_type;
	std::unique_ptr<Vst3Plugin>
			CreateByIndex(size_t index, host_context_type host_context);

	std::unique_ptr<Vst3Plugin>
			CreateByID(Steinberg::int8 const * component_id, host_context_type host_context);

private:
	struct Impl;
	std::unique_ptr<Impl> pimpl_;
};

} // ::hwm