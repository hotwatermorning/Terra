#include "PluginScanner.hpp"

#include <atomic>
#include <thread>
#include <wx/dir.h>
#include <plugin_desc.pb.h>

#include "../misc/ScopeExit.hpp"
#include "../misc/StrCnv.hpp"
#include "../misc/ListenerService.hpp"
#include <pluginterfaces/vst/ivstaudioprocessor.h>

NS_HWM_BEGIN

std::optional<ClassInfo::CID> to_cid(std::string str)
{
    if(str.length() != ClassInfo::CID{}.size()) { return std::nullopt; }
 
    ClassInfo::CID cid = {};
    std::copy_n(str.begin(), cid.size(), cid.begin());
    return cid;
}

template<class Container>
bool Contains(Container &c, ClassInfo::CID const &cid) {
    return std::any_of(c.begin(), c.end(), [&cid](PluginDescription const &desc) {
        return
        desc.type() == PluginDescription_PluginType_VST3
        && desc.has_vst3info()
        && *to_cid(desc.vst3info().cid()) == cid;
    });
}

struct PluginScanner::Impl
{
    Impl()
    {
        scanning_ = false;
    }
    
    std::vector<String> path_to_scan_;
    LockFactory lf_;
    std::vector<PluginDescription> pds_;
    std::thread th_;
    std::atomic<bool> scanning_;
    ListenerService<PluginScanner::Listener> listeners_;
};

class PluginScanner::Traverser
:   public wxDirTraverser
{
public:
    Traverser(PluginScanner &owner)
    :   owner_(owner)
    {}
    
    wxDirTraverseResult OnFile(wxString const &filename) override
    {
        return wxDIR_CONTINUE;
    }
    
    wxDirTraverseResult OnDir(wxString const &dirname) override
    {
        if(dirname.EndsWith(L"vst3") == false) {
            return wxDIR_CONTINUE;
        }
        
        auto factory_list = Vst3PluginFactoryList::GetInstance();
        auto factory = factory_list->FindOrCreateFactory(dirname.ToStdWstring());
        
        if(!factory) {
            return wxDIR_CONTINUE;
        }
        
        auto const num = factory->GetComponentCount();
        for(int i = 0; i < num; ++i) {
            auto info = factory->GetComponentInfo(i);
            
            //! カテゴリがkVstAudioEffectClassでないComponentは、オーディオプラグインではないので無視する。
            if(info.category() != hwm::to_wstr(kVstAudioEffectClass)) {
                continue;
            }
            
            auto lock = owner_.pimpl_->lf_.make_lock();
            auto &pds = owner_.pimpl_->pds_;
            
            if(Contains(pds, info.cid())) { continue; }
            
            PluginDescription desc;
            desc.set_name(to_utf8(info.name()));
            desc.set_type(PluginDescription_PluginType_VST3);
            auto vi = desc.mutable_vst3info();
            vi->set_filepath(dirname.ToUTF8());
            std::string const cid(info.cid().begin(), info.cid().end());
            vi->set_cid(cid);
            vi->set_category(to_utf8(info.category()));
            vi->set_cardinality(info.cardinality());
            
            if(info.has_classinfo2()) {
                auto ci2 = std::make_unique<PluginDescription_Vst3Info_ClassInfo2>();
                ci2->set_subcategories(to_utf8(info.classinfo2().sub_categories()));
                ci2->set_vendor(to_utf8(info.classinfo2().vendor()));
                ci2->set_version(to_utf8(info.classinfo2().version()));
                ci2->set_sdk_version(to_utf8(info.classinfo2().sdk_version()));
                vi->set_allocated_classinfo2(ci2.release());
            }
            
            pds.push_back(desc);
        
            owner_.pimpl_->listeners_.Invoke([this](auto *li) {
                li->OnScanningProgressUpdated(&owner_);
            });
        }
        return wxDIR_CONTINUE;
    }
    
private:
    PluginScanner &owner_;
};

PluginScanner::PluginScanner()
:   pimpl_(std::make_unique<Impl>())
{}

PluginScanner::~PluginScanner()
{}

std::vector<String> const & PluginScanner::GetDirectories() const
{
    auto lock = pimpl_->lf_.make_lock();
    return pimpl_->path_to_scan_;
}

void PluginScanner::AddDirectories(std::vector<String> const &dirs)
{
    auto lock = pimpl_->lf_.make_lock();
    pimpl_->path_to_scan_.insert(pimpl_->path_to_scan_.end(),
                                 dirs.begin(), dirs.end()
                                 );
}

void PluginScanner::SetDirectories(std::vector<String> const &dirs)
{
    auto lock = pimpl_->lf_.make_lock();
    pimpl_->path_to_scan_ = dirs;
}

void PluginScanner::ClearDirectories()
{
    auto lock = pimpl_->lf_.make_lock();
    pimpl_->path_to_scan_.clear();
}

std::vector<PluginDescription> PluginScanner::GetPluginDescriptions() const
{
    auto lock = pimpl_->lf_.make_lock();
    return pimpl_->pds_;
}

void PluginScanner::ClearPluginDescriptions()
{
    auto lock = pimpl_->lf_.make_lock();
    pimpl_->pds_.clear();
}

std::string PluginScanner::Export()
{
    PluginDescriptionList list;
    
    auto pds = GetPluginDescriptions();
    
    for(auto pd: pds) {
        auto dest = list.add_list();
        dest->CopyFrom(pd);
    }
    
    return list.SerializeAsString();
}

void PluginScanner::Import(std::string const &str)
{
    PluginDescriptionList pd_list;
    pd_list.ParseFromString(str);

    std::vector<PluginDescription> new_list;
    for(auto &x: pd_list.list()) {
        PluginDescription desc;
        desc.CopyFrom(x);
        new_list.push_back(desc);
    }
    
    auto lock = pimpl_->lf_.make_lock();
    
    auto &pds = pimpl_->pds_;
    
    for(auto &x: pd_list.list()) {
        if(x.type() == PluginDescription_PluginType_VST3) {
            if(x.has_vst3info() == false) { continue; }
            auto maybe_cid = to_cid(x.vst3info().cid());
            if(!maybe_cid || Contains(pds, *maybe_cid)) { continue; }
            pds.push_back(x);
        }
    }
}

void PluginScanner::AddListener(Listener *li)
{
    pimpl_->listeners_.AddListener(li);
}

void PluginScanner::RemoveListener(Listener const *li)
{
    pimpl_->listeners_.RemoveListener(li);
}

void PluginScanner::ScanAsync()
{
    bool expected = false;
    if(pimpl_->scanning_.compare_exchange_strong(expected, true) == false) {
        return;
    }

    Wait();
    
    pimpl_->th_ = std::thread([this] {
        pimpl_->listeners_.Invoke([this](auto *li) {
            li->OnScanningStarted(this);
        });
        
        auto path_to_scan = GetDirectories();
        
        Traverser tr(*this);
        for(auto path: path_to_scan) {
            wxDir dir(path);
            
            try {
                dir.Traverse(tr);
            } catch(std::exception &e) {
                hwm::dout << "Failed to traverse plugin directories: " << e.what() << std::endl;
            }
        }
        
        pimpl_->scanning_ = false;

        pimpl_->listeners_.Invoke([this](auto *li) {
            li->OnScanningFinished(this);
        });
    });
}

void PluginScanner::Wait()
{
    if(pimpl_->th_.joinable()) {
        pimpl_->th_.join();
    }
}

bool HasPluginCategory(PluginDescription const &desc, std::string category_name)
{
    if(desc.vst3info().has_classinfo2()) {
        return desc.vst3info().classinfo2().subcategories().find(category_name) != std::string::npos;
    } else {
        return false;
    }
}

bool IsEffectPlugin(PluginDescription const &desc)
{
    return HasPluginCategory(desc, "Fx");
}

bool IsInstrumentPlugin(PluginDescription const &desc)
{
    return HasPluginCategory(desc, "Inst");
}

NS_HWM_END
