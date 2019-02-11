#pragma once

#include <vector>

#include "../misc/LockFactory.hpp"
#include "../misc/SingleInstance.hpp"
#include "../misc/ListenerService.hpp"
#include "./vst3/Vst3PluginFactory.hpp"
#include <plugin_desc.pb.h>

NS_HWM_BEGIN

struct PluginScanner
:   SingleInstance<PluginScanner>
{
    PluginScanner();
    ~PluginScanner();
    
    std::vector<String> const & GetDirectories() const;
    void AddDirectories(std::vector<String> const &dirs);
    void SetDirectories(std::vector<String> const &dirs);
    void ClearDirectories();
    
    std::vector<schema::PluginDescription> GetPluginDescriptions() const;
    void ClearPluginDescriptions();

    std::string Export();
    void Import(std::string const &str);
    
    struct Listener : public IListenerBase
    {
    protected:
        Listener() {}
    public:
        virtual
        void OnScanningStarted(PluginScanner *scanner) {}
        
        virtual
        void OnScanningProgressUpdated(PluginScanner *scanner) {}
        
        virtual
        void OnScanningFinished(PluginScanner *scanner) {}
    };
    
    using IListenerService = IListenerService<Listener>;

    IListenerService & GetListeners();
    
    void ScanAsync();
    void Wait();
    void Abort();
    
private:
    class Traverser;
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

bool HasPluginCategory(schema::PluginDescription const &desc, std::string category_name);
bool IsEffectPlugin(schema::PluginDescription const &desc);
bool IsInstrumentPlugin(schema::PluginDescription const &desc);

std::optional<ClassInfo::CID> to_cid(std::string str);

NS_HWM_END
