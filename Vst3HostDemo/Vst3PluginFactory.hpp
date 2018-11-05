#pragma once

#include <array>
#include <memory>
#include <functional>
#include <string>
#include <experimental/optional>

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include "./Vst3Utils.hpp"

NS_HWM_BEGIN

class Vst3Plugin;

struct FactoryInfo
{
public:
	bool discardable				() const;
	bool license_check				() const;
	bool component_non_discardable	() const;
	bool unicode					() const;

	String	vendor	() const;
	String	url		() const;
	String	email	() const;

public:
	FactoryInfo() {}
	FactoryInfo(Steinberg::PFactoryInfo const &info);

private:
	String vendor_;
	String url_;
	String email_;
	Steinberg::int32 flags_;
};

struct ClassInfo2Data
{
	ClassInfo2Data(Steinberg::PClassInfo2 const &info);
	ClassInfo2Data(Steinberg::PClassInfoW const &info);

	String const &	sub_categories() const { return sub_categories_; }
	String const &	vendor() const { return vendor_;; }
	String const &	version() const { return version_; }
	String const &	sdk_version() const { return sdk_version_; }

private:
	String	sub_categories_;
	String	vendor_;
	String	version_;
	String	sdk_version_;
};

struct ClassInfo
{
public:
	ClassInfo(Steinberg::PClassInfo const &info);
	ClassInfo(Steinberg::PClassInfo2 const &info);
	ClassInfo(Steinberg::PClassInfoW const &info);

	Steinberg::int8	const *	cid() const { return cid_.data(); }
	String const &	name() const { return name_; }
	String const &	category() const { return category_; }
	Steinberg::int32        cardinality() const { return cardinality_; }

	bool is_classinfo2_enabled() const { return static_cast<bool>(classinfo2_data_); }
	ClassInfo2Data const &
			classinfo2() const { return *classinfo2_data_; }

private:
	std::array<Steinberg::int8, 16> cid_;
	String		name_;
	String		category_;
	Steinberg::int32	cardinality_;
    std::experimental::optional<ClassInfo2Data> classinfo2_data_;
};

class Vst3PluginFactory
:   std::enable_shared_from_this<Vst3PluginFactory>
{
public:
	Vst3PluginFactory(String module_path);
	~Vst3PluginFactory();

	FactoryInfo const &
			GetFactoryInfo() const;

	//! PClassInfo::category が kVstAudioEffectClass のもののみ
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
    
struct Vst3PluginFactoryList
{
    Vst3PluginFactoryList();
    virtual ~Vst3PluginFactoryList();
    
    std::shared_ptr<Vst3PluginFactory> FindOrCreateFactory(String module_path);
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
