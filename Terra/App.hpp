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
    
    //! do initialization in the dedicated thread
    void OnInitImpl();
    
    void BeforeExit();
    
    class ChangeProjectListener : public ListenerBase
    {
    protected:
        ChangeProjectListener() {}
        
    public:
        virtual
        void OnChangeCurrentProject(Project *prev_pj, Project *new_pj) {}
    };
    void AddChangeProjectListener(ChangeProjectListener *li);
    void RemoveChangeProjectListener(ChangeProjectListener const *li);
    
    std::unique_ptr<Vst3Plugin> CreateVst3Plugin(PluginDescription const &desc);

    void RescanPlugins();
    void ForceRescanPlugins();
    
    //! get added project list.
    //! (currently, this function returns the default project only.)
    std::vector<Project *> GetProjectList();
    
    void SetCurrentProject(Project *pj);
    Project * GetCurrentProject();
    
    //! modal
    void ShowSettingDialog();
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
};

NS_HWM_END
