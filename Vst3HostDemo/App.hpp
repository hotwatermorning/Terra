#pragma once

#include <memory>

#include "Vst3PluginFactory.hpp"
#include "Vst3Plugin.hpp"
#include "AudioDeviceManager.hpp"
#include "TestAudioProcessor.hpp"
#include "ListenerService.hpp"
#include "SingleInstance.hpp"

NS_HWM_BEGIN

class MyApp
:   public wxApp
,   public SingleInstance<MyApp>
{
public:
    using SingleInstance<MyApp>::GetInstance;
    
    bool OnInit() override;
    int OnExit() override;
    
    void BeforeExit();
    
    std::unique_ptr<AudioDeviceManager> adm_;
    
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
        void OnVst3PluginLoaded(Vst3Plugin *plugin) = 0;
        
        virtual
        void OnVst3PluginUnloaded(Vst3Plugin *plugin) = 0;
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
    TestAudioProcessor * GetAudioProcessor();
    
private:
    ListenerService<FactoryLoadListener> fl_listeners_;
    ListenerService<Vst3PluginLoadListener> vl_listeners_;
    std::unique_ptr<Vst3PluginFactory> factory_;
    std::shared_ptr<Vst3Plugin> plugin_;
    std::shared_ptr<TestAudioProcessor> processor_;
    wxString device_name_;
    
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
};

NS_HWM_END
