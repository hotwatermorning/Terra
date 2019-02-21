#pragma once

#include <array>
#include <memory>
#include <functional>
#include <string>

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>

#include "../../misc/SingleInstance.hpp"

NS_HWM_BEGIN

class Vst3Plugin;

class FactoryInfo
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

class ClassInfo2Data
{
public:
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

class ClassInfo
{
public:
    static constexpr UInt32 kCIDLength = 16;
    using CID = std::array<Steinberg::int8, kCIDLength>;
    
    ClassInfo();
	ClassInfo(Steinberg::PClassInfo const &info);
	ClassInfo(Steinberg::PClassInfo2 const &info);
	ClassInfo(Steinberg::PClassInfoW const &info);

    CID const &	cid() const { return cid_; }
	String const &	name() const { return name_; }
	String const &	category() const { return category_; }
	Steinberg::int32        cardinality() const { return cardinality_; }

	bool has_classinfo2() const { return static_cast<bool>(classinfo2_data_); }
	ClassInfo2Data const &
			classinfo2() const { return *classinfo2_data_; }

private:
    CID cid_ = {{}};
	String		name_;
	String		category_;
	Steinberg::int32	cardinality_ = -1;
    std::optional<ClassInfo2Data> classinfo2_data_;
};

class Vst3PluginFactory
{
public:
	Vst3PluginFactory(String module_path);
	~Vst3PluginFactory();

	FactoryInfo const &
			GetFactoryInfo() const;

	//! count number of components where PClassInfo::category is kVstAudioEffectClass.
	size_t GetComponentCount() const;

	ClassInfo const &
			GetComponentInfo(size_t index);

	std::unique_ptr<Vst3Plugin>
			CreateByIndex(size_t index);

	std::unique_ptr<Vst3Plugin>
            CreateByID(ClassInfo::CID const &component_id);
    
    UInt32   GetNumLoadedPlugins() const;

private:
	class Impl;
	std::unique_ptr<Impl> pimpl_;
};
    
class Vst3PluginFactoryList final
:   public SingleInstance<Vst3PluginFactoryList>
{
public:
    Vst3PluginFactoryList();
    ~Vst3PluginFactoryList();
    
    std::shared_ptr<Vst3PluginFactory> FindOrCreateFactory(String module_path);
    
    //! Unload factories which not having any plugins.
    void Shrink();
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
