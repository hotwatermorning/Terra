#include "App.hpp"
#include "gui/GUI.hpp"

#include <wx/cmdline.h>
#include <wx/stdpaths.h>

#include <exception>
#include <algorithm>
#include <fstream>
#include "./misc/StrCnv.hpp"
#include "./plugin/PluginScanner.hpp"
#include "./plugin/vst3/Vst3PluginFactory.hpp"

#include "device/AudioDeviceManager.hpp"
#include "device/MidiDeviceManager.hpp"

NS_HWM_BEGIN

double const kSampleRate = 44100;
SampleCount const kBlockSize = 256;

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
    ListenerService<ProjectActivationListener> pa_listeners_;
    Vst3PluginFactoryList factory_list_;
    std::shared_ptr<Project> project_;
    wxString device_name_;
    
    PluginScanner plugin_scanner_;
    PluginListExporter plugin_list_exporter_;
    
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
    
    pimpl_->plugin_scanner_.AddDirectories({
        L"/Library/Audio/Plug-Ins/VST3",
        wxStandardPaths::Get().GetDocumentsDir().ToStdWstring() + L"../Library/Audio/Plug-Ins/VST3",
        L"../../ext/vst3sdk/build_debug/VST3/Debug",
    });
    
    std::ifstream ifs(GetPluginDescFileName());
    if(ifs) {
        std::string dump_data;
        std::copy(std::istreambuf_iterator<char>(ifs),
                  std::istreambuf_iterator<char>(),
                  std::back_inserter(dump_data)
                  );
        
        pimpl_->plugin_scanner_.Import(dump_data);
    } else {
        pimpl_->plugin_scanner_.ScanAsync();
    }

    pimpl_->adm_ = std::make_unique<AudioDeviceManager>();
    
    auto audio_device_infos = pimpl_->adm_->Enumerate();
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

    auto output_device = find_entry(DeviceIOType::kOutput, 2, pimpl_->adm_->GetDefaultDriver());
    if(!output_device) { output_device = find_entry(DeviceIOType::kOutput, 2); }
    
    if(!output_device) {
        throw std::runtime_error("No audio output devices found");
    }
    
    //! may not found
    auto input_device = find_entry(DeviceIOType::kInput, 2, output_device->driver_);
    
    auto result = pimpl_->adm_->Open(input_device, output_device, kSampleRate, kBlockSize);
    if(result.is_right() == false) {
        throw std::runtime_error(to_utf8(L"Failed to open the device: " + result.left().error_msg_));
    }
    
    //! start the audio device.
    pimpl_->adm_->GetDevice()->Start();

    pimpl_->mdm_ = std::make_unique<MidiDeviceManager>();
    auto midi_device_infos = pimpl_->mdm_->Enumerate();
    for(auto info: midi_device_infos) {
        hwm::wdout
        << L"[{:<6s}] {}"_format((info.io_type_ == DeviceIOType::kInput ? L"Input": L"Output"),
                                 info.name_id_
                                 )
        << std::endl;
        
        auto d = pimpl_->mdm_->Open(info);
        if(info.io_type_ == DeviceIOType::kInput) {
            pimpl_->midi_ins_.push_back(d);
        } else {
            pimpl_->midi_outs_.push_back(d);
        }
    }
    
    auto pj = std::make_shared<Project>();
    pj->SetSequence(MakeSequence());
    pj->GetTransporter().SetLoopRange(0, 4 * kSampleRate);
    pj->GetTransporter().SetLoopEnabled(true);
    
    auto dev = pimpl_->adm_->GetDevice();
    if(auto info = dev->GetDeviceInfo(DeviceIOType::kInput)) {
        pj->AddAudioInput(info->name_, 0, info->num_channels_);
    }
    
    if(auto info = dev->GetDeviceInfo(DeviceIOType::kOutput)) {
        pj->AddAudioOutput(info->name_, 0, info->num_channels_);
    }
        
    for(auto mi: pimpl_->midi_ins_) { pj->AddMidiInput(mi); }
    for(auto mo: pimpl_->midi_outs_) { pj->AddMidiOutput(mo); }
    
    pimpl_->project_ = pj; // activation.

    pimpl_->project_->OnAfterActivated();
    pimpl_->pa_listeners_.Invoke([this](auto *li) {
        li->OnAfterProjectActivated(pimpl_->project_.get());
    });

    MyFrame *frame = new MyFrame( "Vst3HostDemo", wxPoint(50, 50), wxSize(450, 340) );
    frame->Show( true );
    frame->SetFocus();
    frame->SetMinSize(wxSize(400, 300));
    return true;
}

int MyApp::OnExit()
{
    pimpl_->pa_listeners_.Invoke([this](auto *li) {
        li->OnBeforeProjectDeactivated(pimpl_->project_.get());
    });
    pimpl_->project_->OnBeforeDeactivated();
    
    pimpl_->project_.reset(); // dactivation.
    
    for(auto d: pimpl_->midi_ins_) { pimpl_->mdm_->Close(d); }
    for(auto d: pimpl_->midi_outs_) { pimpl_->mdm_->Close(d); }
    
    pimpl_->adm_->Close();
    pimpl_->project_.reset();
    pimpl_->factory_list_.Shrink();
    return 0;
}

void MyApp::BeforeExit()
{}

void MyApp::AddProjectActivationListener(ProjectActivationListener *li) { pimpl_->pa_listeners_.AddListener(li); }
void MyApp::RemoveProjectActivationListener(ProjectActivationListener const *li) { pimpl_->pa_listeners_.RemoveListener(li); }

std::unique_ptr<Vst3Plugin> MyApp::CreateVst3Plugin(PluginDescription const &desc)
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
        return factory->CreateByID(*cid);
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

Project * MyApp::GetProject()
{
    return pimpl_->project_.get();
}

namespace {
    wxCmdLineEntryDesc const cmdline_descs [] =
    {
        { wxCMD_LINE_SWITCH, "h", "help", "show help", wxCMD_LINE_VAL_NONE, wxCMD_LINE_OPTION_HELP },
        { wxCMD_LINE_OPTION, "d", "device", "specify device name", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
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
    parser.Found(wxString("d"), &pimpl_->device_name_);
    return true;
}

NS_HWM_END

wxIMPLEMENT_APP(hwm::MyApp);
