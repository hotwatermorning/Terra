#include "App.hpp"
#include "gui/GUI.hpp"

#include <exception>
#include <algorithm>
#include <fstream>
#include <thread>

#include <wx/cmdline.h>
#include <wx/stdpaths.h>
#include <wx/splash.h>
#include <wx/dir.h>
#include <google/protobuf/util/message_differencer.h>

#include "./misc/StrCnv.hpp"
#include "./misc/FileStream.hpp"
#include "./gui/Util.hpp"
#include "./plugin/PluginScanner.hpp"
#include "./plugin/vst3/Vst3PluginFactory.hpp"

#include "device/AudioDeviceManager.hpp"
#include "device/MidiDeviceManager.hpp"
#include "gui/PCKeyboardInput.hpp"
#include "gui/SettingDialog.hpp"
#include "gui/SplashScreen.hpp"
#include "resource/ResourceHelper.hpp"
#include "file/ProjectObjectTable.hpp"
#include "file/MidiFile.hpp"
#include "log/LoggingSupport.hpp"
#include "log/LoggingStrategy.hpp"

#include <config.pb.h>

NS_HWM_BEGIN

double const kSampleRate = 44100;
SampleCount const kBlockSize = 256;

wxSize const kMinimumWindowSize = { 450, 300 };
wxSize const kDefaultWindowSize = { 640, 500 };

String GetPluginDescFileName() {
    return GetResourcePath(L"plugin_list.bin");
}

struct App::Impl
{
    struct PluginListExporter
    :   PluginScanner::Listener
    {
        void OnScanningFinished(PluginScanner *ps)
        {
#if defined(_MSC_VER)
            std::ofstream ofs(GetPluginDescFileName(), std::ios::out|std::ios::binary);
#else
            std::ofstream ofs(to_utf8(GetPluginDescFileName()), std::ios::out|std::ios::binary);
#endif
            auto str = ps->Export();
            ofs.write(str.data(), str.length());
        }
    };
    
    PCKeyboardInput  pc_keys_;
    std::unique_ptr<AudioDeviceManager> adm_;
    std::unique_ptr<MidiDeviceManager> mdm_;
    std::vector<MidiDevice *> midi_ins_;
    std::vector<MidiDevice *> midi_outs_;
    ListenerService<ChangeProjectListener> cp_listeners_;
    Vst3PluginFactoryList factory_list_;
    std::vector<std::shared_ptr<Project>> projects_;
    Project * current_project_ = nullptr;
    
    PluginScanner plugin_scanner_;
    PluginListExporter plugin_list_exporter_;
    ISplashScreen *splash_screen_ = nullptr;
    wxFrame *main_frame_ = nullptr;
    std::thread initialization_thread_;
    std::vector<String> vst3_paths_;
    
    Impl()
    {
        plugin_scanner_.GetListeners().AddListener(&plugin_list_exporter_);
        vst3_paths_ = App::GetDefaultVst3PluginSearchPaths();
    }
    
    ~Impl()
    {
        plugin_scanner_.GetListeners().RemoveListener(&plugin_list_exporter_);
    }
    
    void LoadConfigImpl(schema::Config const &conf)
    {
        if(conf.has_vst3()) {
            auto &vst3 = conf.vst3();
            
            vst3_paths_.clear();
            for(auto &entry: vst3.paths()) {
                vst3_paths_.push_back(to_wstr(entry));
            }
        }
    }
    
    schema::Config SaveConfigImpl()
    {
        schema::Config conf;
        auto vst3 = conf.mutable_vst3();
        auto paths = vst3->mutable_paths();
        for(auto &entry: vst3_paths_) {
            paths->Add(to_utf8(entry));
        }
        
        return conf;
    }
    
    bool LoadConfig()
    {
        // コンフィグファイルなし
        if(wxFileExists(GetConfigFilePath()) == false) {
            return false;
        }
        
        auto ifs = open_ifstream(GetConfigFilePath(), std::ios::in|std::ios::binary);
        if(!ifs) {
            wxMessageBox("Failed to open the config file", "Error", wxOK);
            return false;
        }
        
        schema::Config conf;
        auto const parsed_successfully = conf.ParseFromIstream(&ifs);
        if(parsed_successfully == false) {
            wxMessageBox("Failed to load config data", "Error", wxOK);
            return false;
        }

        LoadConfigImpl(conf);
        return true;
    }
    
    bool SaveConfig()
    {
        auto conf = SaveConfigImpl();
        wxFileName config_file(GetConfigFilePath());
        wxDir::Make(config_file.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        
        auto ofs = open_ofstream(GetConfigFilePath(), std::ios::binary);
        if(!ofs) {
            wxMessageBox("Failed to open the config file", "Error", wxOK);
            return false;
        }
            
        auto const serialized_successfully = conf.SerializeToOstream(&ofs);
        if(serialized_successfully == false) {
            wxMessageBox("Failed to save config data", "Error", wxOK);
            return false;
        }
        
        return true;
    }
};

App::App()
:   pimpl_(std::make_unique<Impl>())
{
    InitializeDefaultGlobalLogger();
}

App::~App()
{}

bool App::OnInit()
{
    if(!wxApp::OnInit()) { return false; }
    
    wxInitAllImageHandlers();
    
    auto image = GetResourceAs<wxImage>(L"SplashScreen.png");
    assert(image.IsOk());
    pimpl_->splash_screen_ = CreateSplashScreen(image);
    
    auto logger = GetGlobalLogger();
    auto st = std::make_shared<FileLoggingStrategy>(GetTerraDir() + L"/log/Terra.log");
    auto err = st->OpenPermanently();
    if(err) {
        wxMessageBox(L"Can't open log file: " + err.message());
        return false;
    }
    
    logger->SetStrategy(st);
    logger->StartLogging(true);

    EnableErrorCheckAssertionForLoggingMacros(true);
    
    String version_string = L"0.0.1.0";
    TERRA_INFO_LOG(L"Startup Terra (version: " << version_string << L").");
    
    // コンフィグデータの読み込み
    pimpl_->LoadConfig();
    
    pimpl_->plugin_scanner_.SetDirectories(GetVst3PluginSearchPaths());
    
    TERRA_DEBUG_LOG(L"Add plugin directories.");
    
    auto ifs_plugins = open_ifstream(GetPluginDescFileName(), std::ios::binary);
    if(ifs_plugins) {
        ifs_plugins.seekg(0, std::ios::end);
        auto const end = ifs_plugins.tellg();
        ifs_plugins.seekg(0, std::ios::beg);

        std::string dump_data;
        std::copy_n(std::istreambuf_iterator<char>(ifs_plugins), end, std::back_inserter(dump_data));
        
        pimpl_->plugin_scanner_.Import(dump_data);
        
        pimpl_->splash_screen_->AddMessage(L"Import plugin list");
    } else {
        TERRA_INFO_LOG(L"Begin plugin scanning asynchronously");
        pimpl_->plugin_scanner_.ScanAsync();
        pimpl_->splash_screen_->AddMessage(L"Scanning plugins...");
    }
    
    pimpl_->initialization_thread_ = std::thread([this] { OnInitImpl(); });
    
    return true;
}

void App::OnInitImpl()
{
    auto dummy_wait = [](int milliseconds = 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    };
    
    dummy_wait();
    pimpl_->splash_screen_->AddMessage(L"Initialize audio devices");
    
    pimpl_->adm_ = std::make_unique<AudioDeviceManager>();
    auto adm = pimpl_->adm_.get();
    
    auto audio_device_infos = adm->Enumerate();
    for(auto const &info: audio_device_infos) {
        hwm::wdout << L"{} - {}({}ch)"_format(info.name_, to_wstring(info.driver_), info.num_channels_) << std::endl;
    }
    
    auto find_entry = [&list = audio_device_infos](auto io_type,
                              auto min_channels,
                              std::optional<AudioDriverType> driver = std::nullopt,
                              std::optional<String> name = std::nullopt) -> AudioDeviceInfo const *
    {
        auto found = std::find_if(list.begin(), list.end(), [&](auto const &x) {
            if(x.io_type_ != io_type)           { return false; }
            if(name && name != x.name_)         { return false; }
            if(driver && driver != x.driver_)   { return false; }
            if(x.num_channels_ < min_channels)  { return false; }
            return true;
        });
        if(found == list.end()) { return nullptr; }
        else { return &*found; }
    };

    auto output_device = find_entry(DeviceIOType::kOutput, 2, adm->GetDefaultDriver());
    if(!output_device) { output_device = find_entry(DeviceIOType::kOutput, 2); }
    
    if(!output_device) {
        throw std::runtime_error("No audio output devices found");
    }
    
    //! may not found
    auto input_device = find_entry(DeviceIOType::kInput, 2, output_device->driver_);
    
    dummy_wait();
    pimpl_->splash_screen_->AddMessage(L"Open audio device");
    
    auto result = adm->Open(input_device, output_device, kSampleRate, kBlockSize);
    if(result.is_right() == false) {
        throw std::runtime_error(to_utf8(L"Failed to open the device: " + result.left().error_msg_));
    }
    
    dummy_wait();
    pimpl_->splash_screen_->AddMessage(L"Start audio device");
    
    //! start the audio device.
    adm->GetDevice()->Start();
    
    dummy_wait();
    pimpl_->splash_screen_->AddMessage(L"Initialize MIDI devices");
    
    pimpl_->mdm_ = std::make_unique<MidiDeviceManager>();
    auto mdm = pimpl_->mdm_.get();
    auto midi_device_infos = mdm->Enumerate();
    for(auto info: midi_device_infos) {
        hwm::wdout
        << L"[{:<6s}] {}"_format((info.io_type_ == DeviceIOType::kInput ? L"Input": L"Output"),
                                 info.name_id_
                                 )
        << std::endl;
        
        auto d = mdm->Open(info);
        
        dummy_wait();
        auto msg = L"Open MIDI {} Device: {}"_format((info.io_type_ == DeviceIOType::kInput ? L"IN": L"OUT"),
                                                     info.name_id_);
        pimpl_->splash_screen_->AddMessage(msg);
                                           
        
        if(info.io_type_ == DeviceIOType::kInput) {
            pimpl_->midi_ins_.push_back(d);
        } else {
            pimpl_->midi_outs_.push_back(d);
        }
    }
    
    dummy_wait();
    pimpl_->splash_screen_->AddMessage(L"Create empty project");
    
    OnFileNew();
    
    CallAfter([this] {
        pimpl_->splash_screen_->AddMessage(L"Create main window");
        
        wxFrame *frame = CreateMainFrame(kDefaultWindowSize);
        frame->SetMinSize(kMinimumWindowSize);
        frame->CentreOnScreen();
        frame->Layout();
        frame->Show(true);
        frame->SetFocus();

        pimpl_->main_frame_ = frame;
    
        pimpl_->splash_screen_->Close();
        pimpl_->splash_screen_->Raise();
        pimpl_->splash_screen_ = nullptr;
        if(pimpl_->initialization_thread_.joinable()) {
            pimpl_->initialization_thread_.join();
        }
    });
}

int App::OnExit()
{
    if(pimpl_->initialization_thread_.joinable()) {
        pimpl_->initialization_thread_.join();
    }
    
    SetCurrentProject(nullptr);
    pimpl_->projects_.clear();
    
    for(auto d: pimpl_->midi_ins_) { pimpl_->mdm_->Close(d); }
    for(auto d: pimpl_->midi_outs_) { pimpl_->mdm_->Close(d); }
    
    pimpl_->adm_->Close();
    pimpl_->factory_list_.Shrink();
    return 0;
}

void App::BeforeExit()
{
}

App::ChangeProjectListenerService & App::GetChangeProjectListeners()
{
    return pimpl_->cp_listeners_;
}

std::unique_ptr<Vst3Plugin> App::CreateVst3Plugin(schema::PluginDescription const &desc)
{
    hwm::dout << "Load VST3 Module: " << desc.vst3info().filepath() << std::endl;
    
    std::shared_ptr<Vst3PluginFactory> factory;
    try {
        factory = pimpl_->factory_list_.FindOrCreateFactory(to_wstr(desc.vst3info().filepath()));
    } catch(std::exception &e) {
        hwm::dout << "Failed to create a Vst3PluginFactory: " << e.what() << std::endl;
        return nullptr;
    }
    
    auto cid = to_cid(desc.vst3info().cid());
    assert(cid);
    
    try {
        auto plugin = factory->CreateByID(*cid);
        auto activate_all_buses = [](Vst3Plugin *plugin,
                                     Steinberg::Vst::MediaTypes media,
                                     Steinberg::Vst::BusDirections dir)
        {
            auto const num = plugin->GetNumBuses(media, dir);
            for(int i = 0; i < num; ++i) { plugin->SetBusActive(media, dir, i); }
        };
        
        using MT = Steinberg::Vst::MediaTypes;
        using BD = Steinberg::Vst::BusDirections;
        
        activate_all_buses(plugin.get(), MT::kAudio, BD::kInput);
        activate_all_buses(plugin.get(), MT::kAudio, BD::kOutput);
        activate_all_buses(plugin.get(), MT::kEvent, BD::kInput);
        activate_all_buses(plugin.get(), MT::kEvent, BD::kOutput);
        
        return plugin;
    } catch(std::exception &e) {
        hwm::dout << "Failed to create a Vst3Plugin: " << e.what() << std::endl;
        return nullptr;
    }
}

void App::RescanPlugins()
{
    pimpl_->plugin_scanner_.Abort();
    pimpl_->plugin_scanner_.ScanAsync();
}

void App::ForceRescanPlugins()
{
    pimpl_->plugin_scanner_.Abort();
    pimpl_->plugin_scanner_.ClearPluginDescriptions();
    pimpl_->plugin_scanner_.ScanAsync();
}

std::vector<Project *> App::GetProjectList()
{
    std::vector<Project *> ret;
    std::transform(pimpl_->projects_.begin(),
                   pimpl_->projects_.end(),
                   std::back_inserter(ret),
                   [](auto const &x) { return x.get(); });
    
    return ret;
}

template<class Container, class T>
auto contains(Container const &c, T const &t)
{
    return std::find(std::begin(c), std::end(c), t) != std::end(c);
}

void App::SetCurrentProject(Project *pj)
{
    assert(pj == nullptr || contains(GetProjectList(), pj));
 
    auto old_pj = pimpl_->current_project_;
    if(old_pj == nullptr && pj == nullptr) {
        return;
    }
    
    if(old_pj) {
        old_pj->Deactivate();
    }
    
    pimpl_->current_project_ = pj;
    pimpl_->cp_listeners_.Invoke([old_pj, pj](auto li) {
        li->OnChangeCurrentProject(old_pj, pj);
    });
    
    if(pj) {
        pj->Activate();
    }
}

Project * App::GetCurrentProject()
{
    return pimpl_->current_project_;
}

std::unique_ptr<Project> App::CreateInitialProject()
{
    auto pj = std::make_unique<Project>();
    pj->AddSequence(L"Sequence");
    auto seq = pj->GetSequence(0);
    *seq = Sequence(L"Sequencer", {
        { 0,   1920, 48 },
        { 0,   1920, 55 },
        { 0,    240, 62 },
        { 0,    240, 64 },
        { 0,    240, 67 },
        { 0,    240, 72 },
        { 720,  240, 62 },
        { 720,  240, 64 },
        { 720,  240, 67 },
        { 720,  240, 72 },
        { 1440, 240, 62 },
        { 1440, 240, 64 },
        { 1440, 240, 67 },
        { 1440, 240, 72 },
        //-----------------------------
        { 1920 + 0,   1920, 48 },
        { 1920 + 0,   1920, 58 },
        { 1920 + 0,    240, 65 },
        { 1920 + 0,    240, 69 },
        { 1920 + 0,    240, 70 },
        { 1920 + 0,    240, 74 },
        { 1920 + 720,  240, 65 },
        { 1920 + 720,  240, 69 },
        { 1920 + 720,  240, 70 },
        { 1920 + 720,  240, 74 },
        { 1920 + 1200, 240, 65 },
        { 1920 + 1440, 240, 74 },
        { 1920 + 1680, 240, 72 },
        { 1920 + 1680, 240, 67 },
    });
    pj->GetTransporter().SetLoopRange(0, 4 * kSampleRate);
    pj->GetTransporter().SetLoopEnabled(true);
    
    auto adm = AudioDeviceManager::GetInstance();
    auto dev = adm->GetDevice();
    assert(dev);
    
    if(auto info = dev->GetDeviceInfo(DeviceIOType::kInput)) {
        pj->AddAudioInput(info->name_, 0, info->num_channels_);
    }
    
    if(auto info = dev->GetDeviceInfo(DeviceIOType::kOutput)) {
        pj->AddAudioOutput(info->name_, 0, info->num_channels_);
    }
    
    pj->AddDefaultMidiInputs();
    
    for(auto mi: pimpl_->midi_ins_) { pj->AddMidiInput(mi); }
    for(auto mo: pimpl_->midi_outs_) { pj->AddMidiOutput(mo); }
    
    pj->UpdateLastSchema(pj->ToSchema());
    
    return pj;
}

void App::ReplaceProject(std::unique_ptr<Project> pj)
{
    SetCurrentProject(nullptr);
    pimpl_->projects_.clear();
    
    auto p = pj.get();
    pimpl_->projects_.push_back(std::move(pj));
    
    SetCurrentProject(p);
    auto &graph = p->GetGraph();
    for(auto &node: graph.GetNodes()) {
        if(auto vst3 = dynamic_cast<Vst3AudioProcessor *>(node->GetProcessor().get())) {
            auto result = vst3->Load();
            if(!result) {
                wxMessageBox(L"Failed to reload {}"_format(vst3->GetName()));
            }
        }
    }
    
    pimpl_->cp_listeners_.Invoke([p](ChangeProjectListener *li) {
        assert(p->GetLastSchema());
        li->OnAfterLoadProject(p, *p->GetLastSchema());
    });
    
    auto schema = p->ToSchema();
    assert(schema);
    
    pimpl_->cp_listeners_.Invoke([&p, &schema](ChangeProjectListener *li) {
        li->OnBeforeSaveProject(p, *schema);
    });
    
    p->UpdateLastSchema(std::move(schema));
    
    auto windows_title = wxFileName(p->GetFileName()).GetName();
    if(windows_title.empty()) {
        windows_title = L"Untitled";
    }
    
    //! todo: refactor here with callback mechanism.
    if(pimpl_->main_frame_) {
        pimpl_->main_frame_->SetTitle(windows_title);
    }
}

void App::OnFileNew()
{
    bool const saved = OnFileSave(false, true);
    if(!saved) { return; }
    
    ProjectObjectTable scoped_objects;
    
    ReplaceProject(CreateInitialProject());
}

void App::OnFileOpen()
{
    bool const saved = OnFileSave(false, true);
    if(!saved) { return; }
    
    wxFileDialog dlg(nullptr, "Open Project", "", "",
                     "Project File (*.trproj)|*.trproj",
                     wxFD_OPEN|wxFD_FILE_MUST_EXIST);
    if(dlg.ShowModal() == wxID_CANCEL) {
        return;
    }
    
    auto file = dlg.GetPath();
    std::ifstream is(file.ToStdString().c_str());
    
    auto schema = std::make_unique<schema::Project>();

    bool successful = schema->ParseFromIstream(&is);
    if(!successful) {
        wxMessageBox(L"Failed to load file [{}]"_format(file.ToStdWstring()));
        return;
    }
    
    ProjectObjectTable scoped_objects;
    
    auto new_pj = Project::FromSchema(*schema);
    assert(new_pj);
    
    new_pj->UpdateLastSchema(std::move(schema));
    new_pj->SetFileName(wxFileName(file).GetFullName().ToStdWstring());
    new_pj->SetProjectDirectory(wxFileName(wxFileName(file).GetPath(), ""));

    ReplaceProject(std::move(new_pj));
}

wxFileName SelectFileToSave(Project const *pj)
{
    auto dir = pj->GetProjectDirectory();
    if(dir.GetFullPath().empty()) {
        auto path = wxFileName(GetTerraDir(), "");
        if(path.DirExists() == false) {
            path.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        }
        //! todo: get default project location from config.
        path.AppendDir("Projects");
        if(path.DirExists() == false && path.Mkdir() == false) {
            wxMessageBox(L"Failed to create the project directory [{}]."_format(
                                                                                path.GetFullPath().ToStdWstring()
                                                                                ));
            return wxFileName();
        }
        dir = path.GetFullPath();
    }
    
    wxFileDialog dlg(nullptr, "Save Project", dir.GetFullPath(), "",
                     "Project File (*trproj)|*.trproj",
                     wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(dlg.ShowModal() == wxID_CANCEL) {
        return wxFileName();
    }
    
    return wxFileName(dlg.GetPath());
}

bool App::OnFileSave(bool force_save_as, bool need_to_confirm_for_closing)
{
    auto pj = Project::GetCurrentProject();
    if(!pj) { return true; }
    
    auto schema = pj->ToSchema();
    assert(schema);
    
    pimpl_->cp_listeners_.Invoke([&pj, &schema](ChangeProjectListener *li) {
        li->OnBeforeSaveProject(pj, *schema);
    });
    
    if(auto last_schema = pj->GetLastSchema()) {
        using MD = google::protobuf::util::MessageDifferencer;
        MD diff;
        std::string buf;
        diff.set_report_matches(true);
        diff.set_report_moves(true);
        diff.set_message_field_comparison(MD::MessageFieldComparison::EQUIVALENT);
        diff.set_scope(MD::Scope::FULL);
        diff.set_float_comparison(MD::FloatComparison::APPROXIMATE);
        diff.set_repeated_field_comparison(MD::RepeatedFieldComparison::AS_SET);
        diff.ReportDifferencesToString(&buf);
        if(diff.Compare(*schema, *last_schema)) {
            return true;
        } else {
            std::cout << "Diff: " << buf << std::endl;
        }
    }
    
    if(need_to_confirm_for_closing) {
        wxMessageDialog dlg(nullptr,
                            "Do you want to save this project before to close?",
                            "Save",
                            wxYES_NO|wxCANCEL|wxCENTER);
        dlg.SetYesNoCancelLabels("Save", "Discard", "Cancel");
        auto const result = dlg.ShowModal();
        if(result == wxID_NO) {
            return true; // treat as saved.
        } else if(result == wxID_CANCEL){
            return false; // canceled
        }
    }
    
    auto path = pj->GetFullPath();
    if( (path.IsOk() == false) || force_save_as) {
        path = SelectFileToSave(pj);
    }
    
    if(path.IsOk() == false) {
        return false;
    }

    pj->SetFileName(path.GetFullName().ToStdWstring());
    pj->SetProjectDirectory(wxFileName(path.GetPath(), ""));
    schema->set_name(path.GetFullName());
    
    std::ofstream os(path.GetFullPath().ToStdString());
    schema->SerializeToOstream(&os);
    
    pj->UpdateLastSchema(std::move(schema));
    
    return true;
}

void App::LoadProject(String path)
{
#if defined(_MSC_VER)
    std::ifstream is(path.c_str());
#else
    std::ifstream is(to_utf8(path));
#endif
    if(is.fail()) {
        wxMessageBox(L"Cannot open file [{}]"_format(path));
        return;
    }
    
    auto schema = std::make_unique<schema::Project>();
    
    bool successful = schema->ParseFromIstream(&is);
    if(!successful) {
        wxMessageBox(L"Failed to load file [{}]"_format(path));
        return;
    }
    
    ProjectObjectTable scoped_objects;
    
    auto new_pj = Project::FromSchema(*schema);
    assert(new_pj);
    
    new_pj->UpdateLastSchema(std::move(schema));
    new_pj->SetFileName(wxFileName(path).GetFullName().ToStdWstring());
    new_pj->SetProjectDirectory(wxFileName(wxFileName(path).GetPath(), ""));
    
    ReplaceProject(std::move(new_pj));
}

void App::ImportFile(String path)
{
#if defined(_MSC_VER)
    std::ifstream is(path.c_str());
#else
    std::ifstream is(to_utf8(path));
#endif
    
    if(is.fail()) {
        wxMessageBox(L"Cannot open file [{}]"_format(path));
        return;
    }
    
    std::vector<SequencePtr> sequences = CreateSequenceFromSMF(path);
    
    auto pj = Project::GetCurrentProject();
    for(auto &&seq: sequences) {
        pj->AddSequence(std::move(seq));
    }
}

void App::ShowSettingDialog()
{
    auto dialog = CreateSettingDialog(wxGetActiveWindow());
    dialog->ShowModal();
    dialog->Destroy();
    
    auto adm = AudioDeviceManager::GetInstance();
    if(adm->IsOpened()) {
        adm->GetDevice()->Start();
    }
}

std::vector<String> App::GetDefaultVst3PluginSearchPaths()
{
#if defined(_MSC_VER)
    return {
        L"C:/Program Files/Common Files/VST3"
    };
#else
    auto user_vst3_dir = wxFileName(wxStandardPaths::Get().GetDocumentsDir().ToStdWstring() + L"/../Library/Audio/Plug-Ins/VST3");
    user_vst3_dir.Normalize();
    return {
        L"/Library/Audio/Plug-Ins/VST3",
        user_vst3_dir.GetFullPath().ToStdWstring()
    };
#endif
}

std::vector<String> App::GetVst3PluginSearchPaths() const
{
    return pimpl_->vst3_paths_;
}

void App::SetVst3PluginSearchPaths(std::vector<String> new_list)
{
    pimpl_->vst3_paths_ = new_list;
    pimpl_->plugin_scanner_.SetDirectories(new_list);
    pimpl_->SaveConfig();
}

namespace {
    wxCmdLineEntryDesc const cmdline_descs [] =
    {
        { wxCMD_LINE_SWITCH, "h", "help", "show help", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP },
        { wxCMD_LINE_OPTION, "l", "logging-level", "set logging level to (Error|Warn|Info|Debug). the default value is \"Info\"", wxCMD_LINE_VAL_STRING, 0 },
        { wxCMD_LINE_NONE },
    };
}

void App::OnInitCmdLine(wxCmdLineParser& parser)
{
    parser.SetDesc(cmdline_descs);
    parser.SetSwitchChars("-");
}

bool App::OnCmdLineParsed(wxCmdLineParser& parser)
{
    auto logger = GetGlobalLogger();
    wxString level = "Info";
    parser.Found("l", &level);
    level = level.Capitalize();
    logger->SetMostDetailedActiveLoggingLevel(level.ToStdWstring());
    
    return true;
}

NS_HWM_END

#if !defined(TERRA_BUILD_TEST)
wxIMPLEMENT_APP(hwm::App);
#endif
