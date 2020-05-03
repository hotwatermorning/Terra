#pragma once

#include <memory>

#include "plugin/vst3/Vst3PluginFactory.hpp"
#include "plugin/vst3/Vst3Plugin.hpp"
#include "project/Project.hpp"
#include "misc/ListenerService.hpp"
#include "misc/SingleInstance.hpp"
#include <plugin_desc.pb.h>

NS_HWM_BEGIN

class App
:   public wxApp
,   public SingleInstance<App>
{
public:
    App();
    
    virtual
    ~App();
    
    using SingleInstance<App>::GetInstance;
    
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
        
        //! called before the schema of the project is saved.
        /*! this callback is for gui classes to save gui data to the schema.
         */
        virtual
        void OnBeforeSaveProject(Project *pj, schema::Project &schema) {}
        
        //! called before the schema of the project is saved.
        /*! this callback is for gui classes to load gui data from the schema.
         */
        virtual
        void OnAfterLoadProject(Project *pj, schema::Project const &schema) {}
    };
    
    using ChangeProjectListenerService = IListenerService<ChangeProjectListener>;
    
    ChangeProjectListenerService & GetChangeProjectListeners();
    
    //! load a specified plugin.
    /*! @return the created plugin if successful, nullptr otherwise.
     */
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
    
    void LoadProject(String path);
    void ImportFile(String path);
    
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
