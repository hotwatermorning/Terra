#include "App.hpp"
#include "gui/GUI.hpp"

#include <exception>
#include <algorithm>
#include <fstream>
#include <thread>

#include <wx/cmdline.h>
#include <wx/stdpaths.h>
#include <wx/splash.h>

#include "./misc/StrCnv.hpp"
#include "./gui/Util.hpp"
#include "./plugin/PluginScanner.hpp"
#include "./plugin/vst3/Vst3PluginFactory.hpp"

#include "device/AudioDeviceManager.hpp"
#include "device/MidiDeviceManager.hpp"
#include "gui/SettingDialog.hpp"
#include "gui/SplashScreen.hpp"
#include "resource/ResourceHelper.hpp"

NS_HWM_BEGIN

double const kSampleRate = 44100;
SampleCount const kBlockSize = 256;

wxSize const kMinimumWindowSize = { 450, 300 };
wxSize const kDefaultWindowSize = { 640, 500 };

std::string GetPluginDescFileName() {
    return "plugin_list.bin";
}

std::shared_ptr<Sequence> MakeSequence() {
    static auto const tick_to_sample = [](int tick) -> SampleCount {
        return (SampleCount)std::round(tick / 480.0 * 0.5 * kSampleRate);
    };
    
    auto create_note = [](int tick_pos, int tick_length, UInt8 pitch, UInt8 velocity = 64, UInt8 off_velocity = 0) {
        auto sample_pos = tick_to_sample(tick_pos);
        auto sample_end_pos = tick_to_sample(tick_pos + tick_length);
        UInt8 channel = 0;
        return Sequence::Note { sample_pos, sample_end_pos - sample_pos, channel, pitch, velocity, off_velocity };
    };
    
    std::vector<Sequence::Note> notes {
        // C
        create_note(0, 1920, 48),
        create_note(0, 1920, 55),
        create_note(0, 1920, 62),
        create_note(0, 1920, 64),
        create_note(0, 1920, 67),
        create_note(0, 1920, 72),
        
        // Bb/C
        create_note(1920, 1920, 48),
        create_note(1920, 1920, 58),
        create_note(1920, 1920, 65),
        create_note(1920, 1920, 69),
        create_note(1920, 1920, 70),
        create_note(1920, 1920, 74),
        
//        create_note(480, 480, 50),
//        create_note(960, 480, 52),
//        create_note(1440, 480, 53),
//        create_note(1920, 480, 55),
//        create_note(2400, 480, 57),
//        create_note(2880, 480, 59),
//        create_note(3360, 480, 60),
    };
    
    assert(std::is_sorted(notes.begin(), notes.end(), [](auto const &lhs, auto const &rhs) {
        return lhs.pos_ < rhs.pos_;
    }));
    return std::make_shared<Sequence>(notes);
}

struct MyApp::Impl
{
    struct PluginListExporter
    :   PluginScanner::Listener
    {
        void OnScanningFinished(PluginScanner *ps)
        {
            std::ofstream ofs(GetPluginDescFileName());
            auto str = ps->Export();
            ofs.write(str.data(), str.length());
        }
    };
    
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
    ResourceHelper resource_helper_;
    SplashScreen *splash_screen_;
    std::thread initialization_thread_;
    
    Impl()
    {
        plugin_scanner_.AddListener(&plugin_list_exporter_);
    }
    
    ~Impl()
    {
        plugin_scanner_.RemoveListener(&plugin_list_exporter_);
    }
};

MyApp::MyApp()
:   pimpl_(std::make_unique<Impl>())
{}

MyApp::~MyApp()
{}

bool MyApp::OnInit()
{
    if(!wxApp::OnInit()) { return false; }
    
    wxInitAllImageHandlers();
    
    auto image = GetResourceAs<wxImage>(L"SplashScreen.png");
    assert(image.IsOk());
    pimpl_->splash_screen_ = new SplashScreen(image);
    pimpl_->splash_screen_->Show(true);
    pimpl_->splash_screen_->SetFocus();
    
    pimpl_->initialization_thread_ = std::thread([this] { OnInitImpl(); });
    
    return true;
}

void MyApp::OnInitImpl()
{
    wxInitAllImageHandlers();
    
    pimpl_->plugin_scanner_.AddDirectories({
        L"/Library/Audio/Plug-Ins/VST3",
        wxStandardPaths::Get().GetDocumentsDir().ToStdWstring() + L"../Library/Audio/Plug-Ins/VST3",
        L"../../ext/vst3sdk/build_debug/VST3/Debug",
    });
    
    auto dummy_wait = [](int milliseconds = 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    };
    
    std::ifstream ifs(GetPluginDescFileName());
    if(ifs) {
        std::string dump_data;
        std::copy(std::istreambuf_iterator<char>(ifs),
                  std::istreambuf_iterator<char>(),
                  std::back_inserter(dump_data)
                  );
        
        pimpl_->plugin_scanner_.Import(dump_data);
        
        pimpl_->splash_screen_->AddMessage(L"Import plugin list");
    } else {
        pimpl_->plugin_scanner_.ScanAsync();
        pimpl_->splash_screen_->AddMessage(L"Scanning plugins...");
    }

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
    
    auto pj = std::make_shared<Project>();
    pj->SetSequence(MakeSequence());
    pj->GetTransporter().SetLoopRange(0, 4 * kSampleRate);
    pj->GetTransporter().SetLoopEnabled(true);
    
    auto dev = adm->GetDevice();
    if(auto info = dev->GetDeviceInfo(DeviceIOType::kInput)) {
        pj->AddAudioInput(info->name_, 0, info->num_channels_);
    }
    
    if(auto info = dev->GetDeviceInfo(DeviceIOType::kOutput)) {
        pj->AddAudioOutput(info->name_, 0, info->num_channels_);
    }
        
    for(auto mi: pimpl_->midi_ins_) { pj->AddMidiInput(mi); }
    for(auto mo: pimpl_->midi_outs_) { pj->AddMidiOutput(mo); }
    
    pimpl_->projects_.push_back(pj);
    SetCurrentProject(pj.get());
    
    CallAfter([this] {
        pimpl_->splash_screen_->AddMessage(L"Create main window");
        
        MyFrame *frame = new MyFrame(kAppName, wxDefaultPosition, kDefaultWindowSize);
        frame->Show(true);
        pimpl_->splash_screen_->Raise();
        frame->SetFocus();
        frame->CentreOnScreen();
        frame->SetMinSize(kMinimumWindowSize);
    
        pimpl_->splash_screen_->Close();
        pimpl_->splash_screen_ = nullptr;
        if(pimpl_->initialization_thread_.joinable()) {
            pimpl_->initialization_thread_.join();
        }
    });
}

int MyApp::OnExit()
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

void MyApp::BeforeExit()
{
}

void MyApp::AddChangeProjectListener(ChangeProjectListener *li) { pimpl_->cp_listeners_.AddListener(li); }
void MyApp::RemoveChangeProjectListener(ChangeProjectListener const *li) { pimpl_->cp_listeners_.RemoveListener(li); }

std::unique_ptr<Vst3Plugin> MyApp::CreateVst3Plugin(schema::PluginDescription const &desc)
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

void MyApp::RescanPlugins()
{
    pimpl_->plugin_scanner_.ScanAsync();
}

void MyApp::ForceRescanPlugins()
{
    pimpl_->plugin_scanner_.ClearPluginDescriptions();
    pimpl_->plugin_scanner_.ScanAsync();
}

std::vector<Project *> MyApp::GetProjectList()
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

void MyApp::SetCurrentProject(Project *pj)
{
    assert(pj == nullptr || contains(GetProjectList(), pj));
 
    auto old_pj = pimpl_->current_project_;
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

Project * MyApp::GetCurrentProject()
{
    return pimpl_->current_project_;
}

void MyApp::ShowSettingDialog()
{
    auto dialog = CreateSettingDialog(wxGetActiveWindow());
    dialog->ShowModal();
    dialog->Destroy();
    
    auto adm = AudioDeviceManager::GetInstance();
    if(adm->IsOpened()) {
        adm->GetDevice()->Start();
    }
}

namespace {
    wxCmdLineEntryDesc const cmdline_descs [] =
    {
        { wxCMD_LINE_SWITCH, "h", "help", "show help", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP },
        { wxCMD_LINE_NONE },
    };
}

void MyApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    parser.SetDesc(cmdline_descs);
    parser.SetSwitchChars("-");
}

bool MyApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    return true;
}

NS_HWM_END

wxIMPLEMENT_APP(hwm::MyApp);
