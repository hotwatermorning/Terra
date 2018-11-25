#include "App.hpp"
#include "gui/GUI.hpp"

#include <wx/cmdline.h>

#include <exception>
#include <algorithm>

NS_HWM_BEGIN

double const kSampleRate = 44100;
SampleCount const kBlockSize = 256;

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
    std::unique_ptr<AudioDeviceManager> adm_;
    ListenerService<ProjectActivationListener> pa_listeners_;
    ListenerService<FactoryLoadListener> fl_listeners_;
    ListenerService<Vst3PluginLoadListener> vl_listeners_;
    std::unique_ptr<Vst3PluginFactory> factory_;
    std::shared_ptr<Vst3Plugin> plugin_;
    std::shared_ptr<Project> project_;
    wxString device_name_;
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
    
    
    
    pimpl_->project_ = std::make_shared<Project>();
    pimpl_->project_->SetSequence(MakeSequence());
    pimpl_->project_->GetTransporter().SetLoopRange(0, 4 * kSampleRate);
    pimpl_->project_->GetTransporter().SetLoopEnabled(true);
    pimpl_->pa_listeners_.Invoke([this](auto *li) {
        li->OnAfterProjectActivated(pimpl_->project_.get());
    });
    
    pimpl_->adm_ = std::make_unique<AudioDeviceManager>();
    pimpl_->adm_->AddCallback(pimpl_->project_.get());
    
    auto list = pimpl_->adm_->Enumerate();
    for(auto const &info: list) {
        hwm::wdout << L"{} - {}({}ch)"_format(info.name_, to_wstring(info.driver_), info.num_channels_) << std::endl;
    }
    
    auto find_entry = [&list](auto io_type,
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

    auto output_device = find_entry(AudioDeviceIOType::kOutput, 2, pimpl_->adm_->GetDefaultDriver());
    if(!output_device) { output_device = find_entry(AudioDeviceIOType::kOutput, 2); }
    
    if(!output_device) {
        throw std::runtime_error("No devices found");
    }
    
    auto input_device = find_entry(AudioDeviceIOType::kInput, 2, output_device->driver_);
    
    bool const opened = pimpl_->adm_->Open(input_device, output_device, kSampleRate, kBlockSize);
    if(!opened) {
        throw std::runtime_error("Failed to open the device");
    }
    
    pimpl_->adm_->Start();
    
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
    
    pimpl_->adm_->Close();
    pimpl_->project_->RemoveInstrument();
    pimpl_->project_.reset();
    pimpl_->plugin_.reset();
    pimpl_->factory_.reset();
    return 0;
}

void MyApp::BeforeExit()
{
    pimpl_->project_->RemoveInstrument();
}

void MyApp::AddProjectActivationListener(ProjectActivationListener *li) { pimpl_->pa_listeners_.AddListener(li); }
void MyApp::RemoveProjectActivationListener(ProjectActivationListener const *li) { pimpl_->pa_listeners_.RemoveListener(li); }

void MyApp::AddFactoryLoadListener(MyApp::FactoryLoadListener *li) { pimpl_->fl_listeners_.AddListener(li); }
void MyApp::RemoveFactoryLoadListener(MyApp::FactoryLoadListener const *li) { pimpl_->fl_listeners_.RemoveListener(li); }

void MyApp::AddVst3PluginLoadListener(MyApp::Vst3PluginLoadListener *li) { pimpl_->vl_listeners_.AddListener(li); }
void MyApp::RemoveVst3PluginLoadListener(MyApp::Vst3PluginLoadListener const *li) { pimpl_->vl_listeners_.RemoveListener(li); }

bool MyApp::LoadFactory(String path)
{
    hwm::dout << "Load VST3 Module: " << path << std::endl;
    try {
        auto tmp_factory = std::make_unique<Vst3PluginFactory>(path);
        UnloadFactory();
        pimpl_->factory_ = std::move(tmp_factory);
        pimpl_->fl_listeners_.Invoke([path, this](auto *li) {
            li->OnFactoryLoaded(path, pimpl_->factory_.get());
        });
        return true;
    } catch(std::exception &e) {
        hwm::dout << "Create VST3 Factory failed: " << e.what() << std::endl;
        return false;
    }
}

void MyApp::UnloadFactory()
{
    if(!pimpl_->factory_) { return; }
    UnloadVst3Plugin(); // ロード済みのプラグインがあれば、先にアンロードしておく。
    pimpl_->fl_listeners_.InvokeReversed([](auto *li) { li->OnFactoryUnloaded(); });
}

bool MyApp::IsFactoryLoaded() const
{
    return !!pimpl_->factory_;
}

bool MyApp::LoadVst3Plugin(int component_index)
{
    assert(IsFactoryLoaded());
    
    try {
        auto tmp_plugin = pimpl_->factory_->CreateByIndex(component_index);
        UnloadVst3Plugin();
        pimpl_->plugin_ = std::move(tmp_plugin);
        pimpl_->project_->SetInstrument(pimpl_->plugin_);
        pimpl_->vl_listeners_.Invoke([this](auto li) {
            li->OnAfterVst3PluginLoaded(pimpl_->plugin_.get());
        });
        return true;
    } catch(std::exception &e) {
        hwm::dout << "Create VST3 Plugin failed: " << e.what() << std::endl;
        return false;
    }
}

void MyApp::UnloadVst3Plugin()
{
    if(pimpl_->plugin_) {
        pimpl_->project_->RemoveInstrument();
        
        pimpl_->vl_listeners_.InvokeReversed([this](auto li) {
            li->OnBeforeVst3PluginUnloaded(pimpl_->plugin_.get());
        });
        auto tmp = std::move(pimpl_->plugin_);
        tmp.reset();
    }
}

bool MyApp::IsVst3PluginLoaded() const
{
    return !!pimpl_->plugin_;
}

Vst3PluginFactory * MyApp::GetFactory()
{
    return pimpl_->factory_.get();
}

Vst3Plugin * MyApp::GetPlugin()
{
    return pimpl_->plugin_.get();
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
