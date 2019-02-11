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
    
    class ChangeProjectListener : public IListenerBase
    {
    protected:
        ChangeProjectListener() {}
        
    public:
        virtual
        void OnChangeCurrentProject(Project *prev_pj, Project *new_pj) {}
    };
    
    using ChangeProjectListenerService = IListenerService<ChangeProjectListener>;
    
    ChangeProjectListenerService & GetChangeProjectListeners();
    
    std::unique_ptr<Vst3Plugin> CreateVst3Plugin(schema::PluginDescription const &desc);

    void RescanPlugins();
    void ForceRescanPlugins();
    
    //! get added project list.
    //! (currently, this function returns the default project only.)
    std::vector<Project *> GetProjectList();
    
    void SetCurrentProject(Project *pj);
    Project * GetCurrentProject();
    
    void OnFileNew();
    void OnFileOpen();
    //! @return true if saved or no need to save. false if canceled.
    bool OnFileSave(bool force_save_as, bool need_to_confirm_for_closing);
    
    //! modal
    void ShowSettingDialog();
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
    
    std::unique_ptr<Project> CreateInitialProject();
    //! multiple project is not supported yet.
    void ReplaceProject(std::unique_ptr<Project> pj);
    
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
};

NS_HWM_END
