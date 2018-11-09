#include <iostream>

#include "./Vst3HostContext.hpp"

#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>

#include "Vst3Utils.hpp"
#include "Vst3Plugin.hpp"
#include "../../misc/StrCnv.hpp"

NS_HWM_BEGIN

using namespace Steinberg;

Vst3Plugin::HostContext::HostContext(hwm::String host_name)
{
    host_name_ = hwm::to_utf16(host_name);
}

Vst3Plugin::HostContext::~HostContext()
{
    hwm::dout << L"Vst3Plugin::HostContext is now deleted." << std::endl;
}

std::unique_ptr<Steinberg::FUnknown, SelfReleaser>
Vst3Plugin::HostContext::AsUnknownPtr()
{
    Steinberg::FUnknown *p = unknownCast();
    p->addRef();
    return std::unique_ptr<Steinberg::FUnknown, SelfReleaser>(p);
}

void Vst3Plugin::HostContext::SetVst3Plugin(Vst3Plugin *plugin)
{
    plugin_ = plugin;
}

tresult PLUGIN_API Vst3Plugin::HostContext::getName(Vst::String128 name)
{
    auto const length = std::min<int>(host_name_.length(), 128);
    std::copy_n(std::begin(host_name_), length, name);
    
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::createInstance(TUID cid, TUID iid, void **obj)
{
    auto const classID = FUID::fromTUID(cid);
    auto const interfaceID = FUID::fromTUID(iid);
    
    if (classID == Vst::IMessage::iid && interfaceID == Vst::IMessage::iid)
    {
        *obj = new Vst::HostMessage;
        return kResultTrue;
    }
    else if (classID == Vst::IAttributeList::iid && interfaceID == Vst::IAttributeList::iid)
    {
        *obj = new Vst::HostAttributeList;
        return kResultTrue;
    }
    *obj = 0;
    return kResultFalse;
}

tresult PLUGIN_API Vst3Plugin::HostContext::beginEdit (Vst::ParamID id)
{
    hwm::dout << "Begin edit   [" << id << "]" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::performEdit (Vst::ParamID id, Vst::ParamValue valueNormalized)
{
    hwm::dout << "Perform edit [" << id << "]\t[" << valueNormalized << "]" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::endEdit (Vst::ParamID id)
{
    hwm::dout << "End edit     [" << id << "]" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::restartComponent (int32 flags)
{
    hwm::dout << "Restart request has come [" << flags << "]" << std::endl;
    if(plugin_) {
        plugin_->RestartComponent(flags);
    }
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::setDirty (TBool state)
{
    hwm::dout << "Plugin has dirty [" << std::boolalpha << state << "]" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::requestOpenEditor (FIDString name)
{
    hwm::dout << "Open editor request has come [ " << name << "]" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::startGroupEdit ()
{
    hwm::dout << "Begin group edit." << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::finishGroupEdit ()
{
    hwm::dout << "End group edit." << std::endl;
    return kResultOk;
}

NS_HWM_END
