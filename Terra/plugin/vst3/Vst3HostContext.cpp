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

void Vst3Plugin::HostContext::SetVst3Plugin(Vst3Plugin *plugin)
{
    plugin_ = plugin;
}

tresult PLUGIN_API Vst3Plugin::HostContext::getName(Vst::String128 name)
{
    hwm::dout << "HostContext::getName" << std::endl;

    auto const length = std::min<int>(host_name_.length(), 128);
    std::copy_n(std::begin(host_name_), length, name);
    
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::createInstance(TUID cid, TUID iid, void **obj)
{
    auto const classID = FUID::fromTUID(cid);
    auto const interfaceID = FUID::fromTUID(iid);
    
    FUnknown *p = nullptr;
    
    if (classID == Vst::IMessage::iid) {
        p = new Vst::HostMessage();
    } else if(classID == Vst::IAttributeList::iid) {
        p = new Vst::HostAttributeList();
    }
    
    if(!p) { return kNotImplemented; }
    
    return p->queryInterface(iid, obj);
}

tresult PLUGIN_API Vst3Plugin::HostContext::beginEdit (Vst::ParamID id)
{
    hwm::dout << "Begin edit   [{}]"_format(id) << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::performEdit (Vst::ParamID id, Vst::ParamValue valueNormalized)
{
    hwm::dout << "Perform edit [{}]\t[{}]"_format(id, valueNormalized) << std::endl;
    plugin_->EnqueueParameterChange(id, valueNormalized);
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::endEdit (Vst::ParamID id)
{
    hwm::dout << "End edit     [{}]"_format(id) << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::restartComponent (int32 flags)
{
    hwm::dout << "restartComponent [{}]"_format(flags) << std::endl;
    auto app = wxApp::GetInstance();
    if(wxThread::IsMain() == false) {
        app->CallAfter([this, flags] { restartComponent(flags); });
        return kResultOk;
    }
    
    std::string str;
    auto add_str = [&](auto value, std::string name) {
        if((flags & value) != 0) {
            str += (str.empty() ? "" : ", ") + name;
        }
    };
    
    add_str(Vst::kReloadComponent, "Reload Component");
    add_str(Vst::kIoChanged, "IO Changed");
    add_str(Vst::kParamValuesChanged, "Param Value Changed");
    add_str(Vst::kLatencyChanged, "Latency Changed");
    add_str(Vst::kParamTitlesChanged, "Param Title Changed");
    add_str(Vst::kMidiCCAssignmentChanged, "MIDI CC Assignment Changed");
    add_str(Vst::kNoteExpressionChanged, "Note Expression Changed");
    add_str(Vst::kIoTitlesChanged, "IO Titles Changed");
    add_str(Vst::kPrefetchableSupportChanged, "Prefetchable Support Changed");
    add_str(Vst::kRoutingInfoChanged, "Routing Info Changed");
    
    hwm::dout << "Restart request has come [{}]"_format(str) << std::endl;
    if(plugin_) {
        plugin_->RestartComponent(flags);
    }
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::setDirty (TBool state)
{
    hwm::dout << "Plugin has dirty [{}]"_format(state != 0) << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::requestOpenEditor (FIDString name)
{
    hwm::dout << "Open editor request has come [{}]"_format(name) << std::endl;
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

tresult Vst3Plugin::HostContext::notifyUnitSelection (UnitID unitId)
{
    hwm::dout << "notifyUnitSelection" << std::endl;
    return kResultOk;
}

tresult Vst3Plugin::HostContext::notifyProgramListChange (ProgramListID listId, int32 programIndex)
{
    hwm::dout << "notifyProgramListChange" << std::endl;
    return kResultOk;
}

tresult Vst3Plugin::HostContext::notifyUnitByBusChange ()
{
    hwm::dout << "notifyUnitByBusChange" << std::endl;
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::HostContext::resizeView (IPlugView* view, ViewRect* newSize)
{
    hwm::dout << "resizeView" << std::endl;
    assert(newSize);
    ViewRect current;
    view->getSize(&current);
    
    auto const to_tuple = [](auto x) { return std::tie(x.left, x.top, x.right, x.bottom); };

    //! 同じ位置とサイズのままresizeViewが呼ばれることはない？
    //assert(to_tuple(current) != to_tuple(*newSize));
    
    plug_frame_listener_->OnResizePlugView(*newSize);
    view->onSize(newSize);
    return 0;
}

tresult Vst3Plugin::HostContext::isPlugInterfaceSupported(const TUID iid)
{
    std::vector<FUID> supported {
        Vst::IComponent::iid,
        Vst::IAudioProcessor::iid,
        Vst::IEditController::iid,
        Vst::IEditController2::iid,
        Vst::IConnectionPoint::iid,
        Vst::IUnitInfo::iid,
        Vst::IProgramListData::iid,
        Vst::IMidiMapping::iid
    };
    
    auto const pred = [y = FUID::fromTUID(iid)](auto const &x) { return x == y; };
    
    if(std::any_of(supported.begin(), supported.end(), pred)) {
        return kResultTrue;
    } else {
        return kResultFalse;
    }
}

NS_HWM_END
