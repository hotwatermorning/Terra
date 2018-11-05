#include <iostream>

#include "./Vst3HostCallback.hpp"

#include "./pluginterfaces/vst/ivsteditcontroller.h"
#include "./public.sdk/source/vst/hosting/hostclasses.h"
#include "./public.sdk/source/vst/hosting/parameterchanges.h"

#include "./Vst3Utils.hpp"

NS_HWM_BEGIN

using namespace Steinberg;

struct Vst3HostCallback::Impl
:    public FObject
,    public Vst::IHostApplication
,    public Vst::IComponentHandler
,    public Vst::IComponentHandler2
{
    typedef Impl this_type;
    typedef Vst3HostCallback::request_to_restart_handler_t request_to_restart_handler_t;
    typedef Vst3HostCallback::parameter_change_notification_handler_t parameter_change_notification_handler_t;
    
    OBJ_METHODS(Impl, FObject)
    REFCOUNT_METHODS(FObject)
    
public:
    DEFINE_INTERFACES
    DEF_INTERFACE(Vst::IHostApplication)
    DEF_INTERFACE(Vst::IComponentHandler)
    DEF_INTERFACE(Vst::IComponentHandler2)
    END_DEFINE_INTERFACES(FObject)
    
public:
    Impl() {}
    ~Impl()
    {
        hwm::dout << L"Vst3Host::Impl is now deleted." << std::endl;
    }
    
    void SetRequestToRestartHandler(request_to_restart_handler_t handler)
    {
        request_to_restart_handler_ = handler;
    }
    
    void SetParameterChangeNotificationHandler(parameter_change_notification_handler_t handler)
    {
        parameter_change_notification_handler_ = handler;
    }
    
    void SetParameter(Vst::ParamID id, Vst::ParamValue value)
    {
        beginEdit(id);
        performEdit(id, value);
        endEdit(id);
    }
    
private:
    request_to_restart_handler_t request_to_restart_handler_;
    parameter_change_notification_handler_t parameter_change_notification_handler_;
    
protected:
    // IHostApplication
    tresult PLUGIN_API getName (Vst::String128 name) override;
    tresult PLUGIN_API createInstance (TUID cid, TUID iid, void** obj) override;
    
    //! IComponentHandler
    //! @{
    
    /** To be called before calling a performEdit (e.g. on mouse-click-down event). */
    tresult PLUGIN_API beginEdit (Vst::ParamID id) override
    {
        hwm::dout << "Begin edit   [" << id << "]" << std::endl;
        return kResultOk;
    }
    
    /** Called between beginEdit and endEdit to inform the handler that a given parameter has a new value. */
    tresult PLUGIN_API performEdit (Vst::ParamID id, Vst::ParamValue valueNormalized) override
    {
        hwm::dout << "Perform edit [" << id << "]\t[" << valueNormalized << "]" << std::endl;
        parameter_change_notification_handler_(id, valueNormalized);
        return kResultOk;
    }
    
    /** To be called after calling a performEdit (e.g. on mouse-click-up event). */
    tresult PLUGIN_API endEdit (Vst::ParamID id) override
    {
        hwm::dout << "End edit     [" << id << "]" << std::endl;
        return kResultOk;
    }
    
    /** Instructs host to restart the component. This should be called in the UI-Thread context!
     \param flags is a combination of RestartFlags */
    tresult PLUGIN_API restartComponent (int32 flags) override
    {
        hwm::dout << "Restart request has come [" << flags << "]" << std::endl;
        request_to_restart_handler_(flags);
        return kResultOk;
    }
    
    //@}
    
    //! IComponentHandler2
    //! @{
    
    //------------------------------------------------------------------------
    /** Tells host that the Plug-in is dirty (something besides parameters has changed since last save),
     if true the host should apply a save before quitting. */
    tresult PLUGIN_API setDirty (TBool state) override
    {
        hwm::dout << "Plugin has dirty [" << std::boolalpha << state << "]" << std::endl;
        return kResultOk;
    }
    
    /** Tells host that it should open the Plug-in editor the next time it's possible.
     You should use this instead of showing an alert and blocking the program flow (especially on loading projects). */
    tresult PLUGIN_API requestOpenEditor (FIDString name = Vst::ViewType::kEditor) override
    {
        hwm::dout << "Open editor request has come [ " << name << "]" << std::endl;
        return kResultOk;
    }
    
    //------------------------------------------------------------------------
    /** Starts the group editing (call before a \ref IComponentHandler::beginEdit),
     the host will keep the current timestamp at this call and will use it for all \ref IComponentHandler::beginEdit
     / \ref IComponentHandler::performEdit / \ref IComponentHandler::endEdit calls until a \ref finishGroupEdit (). */
    tresult PLUGIN_API startGroupEdit () override
    {
        hwm::dout << "Begin group edit." << std::endl;
        return kResultOk;
    }
    
    /** Finishes the group editing started by a \ref startGroupEdit (call after a \ref IComponentHandler::endEdit). */
    tresult PLUGIN_API finishGroupEdit () override
    {
        hwm::dout << "End group edit." << std::endl;
        return kResultOk;
    }
    
    //! @}
};

tresult PLUGIN_API Vst3HostCallback::Impl::getName(Vst::String128 name)
{
    std::u16string host_name = u"Vst3HostDemo";
    std::copy(std::begin(host_name),
              std::end(host_name),
              name);
    
    return kResultOk;
}

tresult PLUGIN_API Vst3HostCallback::Impl::createInstance(TUID cid, TUID iid, void **obj)
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

Vst3HostCallback::Vst3HostCallback()
:    pimpl_(new Impl())
{}

Vst3HostCallback::~Vst3HostCallback()
{}

std::unique_ptr<Steinberg::FUnknown, SelfReleaser>
Vst3HostCallback::GetUnknownPtr()
{
    Steinberg::FUnknown *p = pimpl_.get()->unknownCast();
    p->addRef();
    return std::unique_ptr<Steinberg::FUnknown, SelfReleaser>(p);
}

void Vst3HostCallback::SetRequestToRestartHandler(request_to_restart_handler_t handler)
{
    pimpl_->SetRequestToRestartHandler(handler);
}

void Vst3HostCallback::SetParameterChangeNotificationHandler(parameter_change_notification_handler_t handler)
{
    pimpl_->SetParameterChangeNotificationHandler(handler);
}

NS_HWM_END
