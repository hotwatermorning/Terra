#pragma once

#include <memory>

#include "plugin/vst3/Vst3PluginFactory.hpp"
#include "plugin/vst3/Vst3Plugin.hpp"
#include "project/Project.hpp"
#include "misc/ListenerService.hpp"
#include "misc/SingleInstance.hpp"
#include <plugin_desc.pb.h>

NS_HWM_BEGIN

class MyApp
:   public wxApp
,   public SingleInstance<MyApp>
{
public:
    MyApp();
    
    virtual
    ~MyApp();
    
    using SingleInstance<MyApp>::GetInstance;
    
    bool OnInit() override;
    int OnExit() override;
    
    void BeforeExit();
    
    class ProjectActivationListener
    {
    public:
        virtual
        ~ProjectActivationListener() {}
        
        virtual
        void OnAfterProjectActivated(Project *pj) {}
        
        virtual
        void OnBeforeProjectDeactivated(Project *pj) {}
    };
    void AddProjectActivationListener(ProjectActivationListener *li);
    void RemoveProjectActivationListener(ProjectActivationListener const *li);
    
    std::unique_ptr<Vst3Plugin> CreateVst3Plugin(PluginDescription const &desc);

    void RescanPlugins();
    void ForceRescanPlugins();
    
    Project * GetProject();
    
    //! modal
    void ShowSettingDialog();
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
};

NS_HWM_END
