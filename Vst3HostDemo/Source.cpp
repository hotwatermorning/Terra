//#include <windows.h>
//#include <tchar.h>
//
//#include <balor/gui/all.hpp>
//#include <balor/locale/all.hpp>
//#include "namespace.hpp"
//
//#include "VstHostDemo.hpp"
//#include "./Vst3Plugin.hpp"
//
//namespace hwm {
//
//static size_t const sampling_rate = 44100;
//static size_t const block_size = 1024;
//static size_t const num_channels = 2;
//
//int main_impl()
//{
//    VstHostDemo demo_application_;
//
//    bool const opened = demo_application_.OpenDevice(sampling_rate, num_channels, block_size);
//    if(!opened) {
//        return -1;
//    }
//
//    demo_application_.Run();
//
//    return 0;
//}
//
//}    //::hwm
//
//int APIENTRY WinMain(HINSTANCE , HINSTANCE , LPSTR , int ) {
//
//    try {
//        hwm::main_impl();
//    } catch(std::exception &e) {
//        balor::gui::MessageBox::show(
//            balor::String(
//                _T("error : ")) + 
//                balor::locale::Charset(932, true).decode(e.what())
//                );
//    }
//}

// wxWidgets "Hello world" Program
// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <stdio.h>
#include <math.h>
#include <portaudio.h>
#include <iostream>

#include "./Vst3PluginFactory.hpp"
#include "./Vst3HostCallback.hpp"
#include "./Vst3Plugin.hpp"
#include "./Buffer.hpp"
#include "./StrCnv.hpp"
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#define NUM_SECONDS   (4)
#define SAMPLE_RATE   (44100)
#define FRAMES_PER_BUFFER  (64)

#ifndef M_PI
#define M_PI  (3.14159265)
#endif

hwm::Vst3Plugin *g_plugin;
std::vector<int> const g_notes = { 48, 50, 52, 53 };
int g_last_note_index = -1;
int g_current_pos = 0;
PaTime begin = -1;

/* This routine will be called by the PortAudio engine when audio is needed.
 ** It may called at interrupt level on some machines so don't do anything
 ** that could mess up the system like calling malloc() or free().
 */
static int patestCallback( const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData )
{
    auto *finished = reinterpret_cast<bool *>(userData);
    
    if(begin == -1) {
        begin = timeInfo->currentTime;
    }
    
    PaTime elapsed = timeInfo->currentTime - begin;
    if(elapsed > 5) {
        *finished = true;
    }
    
    assert(g_plugin);
    
    int note_index = (int)(elapsed) % g_notes.size();
    if(note_index != g_last_note_index) {
        if(g_last_note_index >= 0) { g_plugin->AddNoteOff(g_notes[note_index]); }
        g_plugin->AddNoteOn(g_notes[note_index]);
    }
    g_last_note_index = note_index;
    
    float *out = (float*)outputBuffer;
    
    (void) timeInfo; /* Prevent unused variable warnings. */
    (void) statusFlags;
    (void) inputBuffer;
    
    auto const result = g_plugin->ProcessAudio(g_current_pos, framesPerBuffer);
    for(int i = 0; i<framesPerBuffer; i++ ) {
        *out++ = result[0][i];
        *out++ = result[1][i];
    }
    g_current_pos += framesPerBuffer;
    
    return paContinue;
}

/*
 * This routine is called by portaudio when playback is done.
 */
static void StreamFinished( void* userData )
{
    printf( "Stream Completed.\n" );
}

class MyApp: public wxApp
{
public:
    virtual bool OnInit();
};
class MyFrame: public wxFrame
{
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
private:
    void OnHello(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnMove(wxMouseEvent &event);
    void OnMove2(wxMouseEvent &event);
    void OnPlay();
    void OnTimer();
    
private:
    std::string msg_;
    wxStaticText *text_;
    wxTimer timer_;
    
    hwm::Vst3HostCallback host_context;
    std::unique_ptr<hwm::Vst3Plugin> plugin;
    hwm::Vst3Plugin *pplugin = nullptr;
    PaStream *stream = nullptr;
    bool finished;
    std::unique_ptr<hwm::Vst3PluginFactory> factory;
};
enum
{
    ID_Hello = 1
};

wxIMPLEMENT_APP(MyApp);
bool MyApp::OnInit()
{
    MyFrame *frame = new MyFrame( "Hello World", wxPoint(50, 50), wxSize(450, 340) );
    frame->Show( true );
    frame->SetFocus();
    return true;
}
MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
: wxFrame(NULL, wxID_ANY, title, pos, size)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_Hello, "&Hello...\tCtrl-H",
                     "Help string shown in status bar for this menu item");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);
    wxMenu *menuPlay = new wxMenu;
    auto id_play = NewControlId();
    menuPlay->Append(id_play, "&Play...\tCtrl-P", "Start playback");
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    menuBar->Append( menuPlay, "&Play" );
    menuBar->Append( menuHelp, "&Help" );
    SetMenuBar( menuBar );
    CreateStatusBar();
    SetStatusText( "Welcome to wxWidgets!" );
    
    text_ = new wxStaticText(this, 100, "hello", wxPoint(10, 10), wxSize(100, 100));
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnHello(ev); }, ID_Hello);
    Bind(wxEVT_MENU, [this](auto &ev) { OnExit(ev); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](auto &ev) { OnAbout(ev); }, wxID_ABOUT);
    Bind(wxEVT_MOTION, [this](auto &ev) { OnMove(ev); });
    text_->Bind(wxEVT_MOTION, [this](auto &ev) { OnMove2(ev); });
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnPlay(); }, id_play);
    
    String path = L"/Library/Audio/Plug-Ins/VST3/Zebra2.vst3";
    factory = std::make_unique<hwm::Vst3PluginFactory>(path);
    
    host_context.SetRequestToRestartHandler([this](Steinberg::int32 flags) {
        if(pplugin) {
            pplugin->RestartComponent(flags);
        }
    });
    
    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    timer_.Start(1000);
}
void MyFrame::OnExit(wxCommandEvent& event)
{
    Close( true );
}
void MyFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox( "This is a wxWidgets' Hello world sample",
                 "About Hello World", wxOK | wxICON_INFORMATION );
}
void MyFrame::OnHello(wxCommandEvent& event)
{
    wxLogMessage("Hello world from wxWidgets!");
}
void MyFrame::OnMove(wxMouseEvent &event)
{
    auto pos = event.GetPosition();
    text_->SetLabel("(" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ")");
}
void MyFrame::OnMove2(wxMouseEvent &event)
{
    text_->SetLabel("On Me");
}

auto showError(PaError err) {
    if(err != paNoError) {
        std::cout << Pa_GetErrorText(err) << std::endl;
    }
}

void MyFrame::OnPlay()
{
    if(plugin) { return; }

    finished = false;
    begin = -1;
    
    std::vector<int> effect_indices;
    auto const cmp_count = factory->GetComponentCount();
    std::cout << "Component Count : " << cmp_count << std::endl;
    
    for(int i = 0; i < cmp_count; ++i) {
        auto const &info = factory->GetComponentInfo(i);
        std::cout << hwm::to_utf8(info.name()) << ", " << hwm::to_utf8(info.category()) << std::endl;
        
        //! カテゴリがkVstAudioEffectClassなComponentを探索する。
        if(info.category() == hwm::to_wstr(kVstAudioEffectClass)) {
            effect_indices.push_back(i);
        }
    }
    
    if(effect_indices.empty()) {
        std::cout << "No AudioEffects found." << std::endl;
        assert(false);
    }
    
    plugin = factory->CreateByIndex(effect_indices[0], host_context.GetUnknownPtr());
    pplugin = plugin.get();

    PaStreamParameters outputParameters;
    PaError err;
    
    printf("PortAudio Test: output sine wave. SR = %d, BufSize = %d\n", SAMPLE_RATE, FRAMES_PER_BUFFER);
    
    err = Pa_Initialize();
    if( err != paNoError ) {
        showError(err);
        assert(false);
    }
    
    outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
    if (outputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default output device.\n");
        assert(false);
    }
    outputParameters.channelCount = 2;       /* stereo output */
    outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = Pa_GetDeviceInfo( outputParameters.device )->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;
    
    plugin->SetBlockSize(FRAMES_PER_BUFFER);
    plugin->SetSamplingRate(SAMPLE_RATE);
    plugin->Resume();
    g_plugin = plugin.get();
    
    err = Pa_OpenStream(&stream,
                        NULL, /* no input */
                        &outputParameters,
                        SAMPLE_RATE,
                        FRAMES_PER_BUFFER,
                        paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                        patestCallback,
                        &finished );
    
    if( err != paNoError ) {
        showError(err);
        assert(false);
    }
    
    err = Pa_SetStreamFinishedCallback( stream, &StreamFinished );
    if( err != paNoError ) {
        showError(err);
        assert(false);
    }
    
    err = Pa_StartStream( stream );
    if( err != paNoError ) {
        showError(err);
        assert(false);
    }
}

void MyFrame::OnTimer()
{
    if(plugin && finished) {
        PaError err;
        
        err = Pa_StopStream( stream );
        if( err != paNoError ) {
            showError(err);
            assert(false);
        }
        
        err = Pa_CloseStream( stream );
        if( err != paNoError ) {
            showError(err);
            assert(false);
        }
        
        Pa_Terminate();
        printf("Test finished.\n");
        
        plugin->Suspend();
        plugin.reset();
        pplugin = nullptr;
        stream = nullptr;
    }
}
