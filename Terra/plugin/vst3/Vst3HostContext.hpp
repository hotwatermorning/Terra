#pragma once

#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstpluginterfacesupport.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/parameterchanges.h>
#include <pluginterfaces/gui/iplugview.h>

#include "Vst3Plugin.hpp"

NS_HWM_BEGIN

using namespace Steinberg;

class Vst3Plugin::HostContext
:   public FObject
,   public Vst::IHostApplication
,   public Vst::IComponentHandler
,   public Vst::IComponentHandler2
,   public Vst::IUnitHandler
,   public Vst::IUnitHandler2
,   public IPlugFrame
,   public Vst::IPlugInterfaceSupport
{
public:
    typedef Impl this_type;
    
    OBJ_METHODS(HostContext, FObject)
    REFCOUNT_METHODS(FObject)
    
public:
    DEFINE_INTERFACES
    DEF_INTERFACE(FObject)
    DEF_INTERFACE(Vst::IHostApplication)
    DEF_INTERFACE(Vst::IComponentHandler)
    DEF_INTERFACE(Vst::IComponentHandler2)
    DEF_INTERFACE(Vst::IUnitHandler)
    DEF_INTERFACE(Vst::IUnitHandler2)
    DEF_INTERFACE(IPlugFrame)
    END_DEFINE_INTERFACES(FObject)
    
public:
    HostContext(String host_name);
    ~HostContext();
    
    Vst3Plugin *plugin_ = nullptr;
    PlugFrameListener *plug_frame_listener_ = nullptr;
    std::u16string host_name_;
    
    void SetVst3Plugin(Vst3Plugin *plugin);
    
protected:
    // IHostApplication
    tresult PLUGIN_API getName (Vst::String128 name) override;
    tresult PLUGIN_API createInstance (TUID cid, TUID iid, void** obj) override;
    
    //! IComponentHandler
    //! @{
    
    /** To be called before calling a performEdit (e.g. on mouse-click-down event). */
    tresult PLUGIN_API beginEdit (Vst::ParamID id) override;
    
    /** Called between beginEdit and endEdit to inform the handler that a given parameter has a new value. */
    tresult PLUGIN_API performEdit (Vst::ParamID id, Vst::ParamValue valueNormalized) override;
    
    /** To be called after calling a performEdit (e.g. on mouse-click-up event). */
    tresult PLUGIN_API endEdit (Vst::ParamID id) override;
    
    /** Instructs host to restart the component. This should be called in the UI-Thread context!
     \param flags is a combination of RestartFlags */
    tresult PLUGIN_API restartComponent (int32 flags) override;
    
    //@}
    
    //! IComponentHandler2
    //! @{
    
    //------------------------------------------------------------------------
    /** Tells host that the Plug-in is dirty (something besides parameters has changed since last save),
     if true the host should apply a save before quitting. */
    tresult PLUGIN_API setDirty (TBool state) override;
    
    /** Tells host that it should open the Plug-in editor the next time it's possible.
     You should use this instead of showing an alert and blocking the program flow (especially on loading projects). */
    tresult PLUGIN_API requestOpenEditor (FIDString name = Vst::ViewType::kEditor) override;
    
    //------------------------------------------------------------------------
    /** Starts the group editing (call before a \ref IComponentHandler::beginEdit),
     the host will keep the current timestamp at this call and will use it for all \ref IComponentHandler::beginEdit
     / \ref IComponentHandler::performEdit / \ref IComponentHandler::endEdit calls until a \ref finishGroupEdit (). */
    tresult PLUGIN_API startGroupEdit () override;
    
    /** Finishes the group editing started by a \ref startGroupEdit (call after a \ref IComponentHandler::endEdit). */
    tresult PLUGIN_API finishGroupEdit () override;
    
    //! @}
    
    tresult notifyUnitSelection (UnitID unitId) override;
    tresult notifyProgramListChange (ProgramListID listId, int32 programIndex) override;
    tresult notifyUnitByBusChange () override;

    //! @name IPlugFrame
    //! @{
    tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override;
    //! @}
    
    //! @name IPlugInterfaceSupport
    //! @{
    tresult isPlugInterfaceSupported(const TUID iid) override;
    //! @}
};

NS_HWM_END
