#pragma once

#include <memory>

#include "plugin/vst3/Vst3PluginFactory.hpp"
#include "plugin/vst3/Vst3Plugin.hpp"
#include "device/AudioDeviceManager.hpp"
#include "project/Project.hpp"
#include "misc/ListenerService.hpp"
#include "misc/SingleInstance.hpp"

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
    
    class FactoryLoadListener
    {
    public:
        virtual
        ~FactoryLoadListener() {}
        
        virtual
        void OnFactoryLoaded(String path, Vst3PluginFactory *factory) = 0;
        
        virtual
        void OnFactoryUnloaded() = 0;
    };
    
    void AddFactoryLoadListener(FactoryLoadListener *li);
    void RemoveFactoryLoadListener(FactoryLoadListener const *li);
    
    class Vst3PluginLoadListener
    {
    public:
        virtual
        ~Vst3PluginLoadListener() {}
        
        virtual
        void OnAfterVst3PluginLoaded(Vst3Plugin *plugin) {};
        
        virtual
        void OnBeforeVst3PluginUnloaded(Vst3Plugin *plugin) {};
    };
    
    void AddVst3PluginLoadListener(Vst3PluginLoadListener *li);
    void RemoveVst3PluginLoadListener(Vst3PluginLoadListener const *li);
    
    bool LoadFactory(String path);
    void UnloadFactory();
    bool IsFactoryLoaded() const;
    
    bool LoadVst3Plugin(int component_index);
    void UnloadVst3Plugin();
    bool IsVst3PluginLoaded() const;
    
    Vst3PluginFactory * GetFactory();
    Vst3Plugin * GetPlugin();
    Project * GetProject();
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
};

NS_HWM_END
