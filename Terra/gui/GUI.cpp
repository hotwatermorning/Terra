#include "./GUI.hpp"
#include "../App.hpp"
#include "../project/Project.hpp"

#include <wx/stdpaths.h>
#include <wx/display.h>
#include <wx/dnd.h>
#include <wx/tglbtn.h>

#include <vector>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../misc/StrCnv.hpp"
#include "../misc/MathUtil.hpp"
#include "../plugin/PluginScanner.hpp"
#include "./Controls.hpp"
#include "./PluginEditor.hpp"
#include "./Keyboard.hpp"
#include "./UnitData.hpp"
#include "./GraphEditor.hpp"
#include "../resource/ResourceHelper.hpp"
#include "./PianoRoll.hpp"
#include "./PCKeyboardInput.hpp"

#if !defined(_MSC_VER)
#include "./OSXMenuBar.h"
#endif

NS_HWM_BEGIN

enum
{
    ID_Play = 1,
    ID_RescanPlugin,
    ID_ForceRescanPlugin,
    ID_Setting,
    ID_File_New,
    ID_File_Open,
    ID_File_Save,
    ID_File_SaveAs,
    ID_View_ShowPianoRoll,
};

class TransportPanel
:   public wxPanel
,   App::ChangeProjectListener
,   Transporter::ITransportStateListener
{
    static
    String GetImagePath(String filename)
    {
        return GetResourcePath({L"transport", filename});
    }
    
public:
    TransportPanel(wxWindow *parent)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    {
        asset_ = ImageAsset(GetImagePath(L"transport_buttons.png"), 6, 4);

        btn_rewind_     = new ImageButton(this, false, asset_.GetImage(0, 0), asset_.GetImage(0, 1), asset_.GetImage(0, 0), asset_.GetImage(0, 1));
        btn_stop_       = new ImageButton(this, false, asset_.GetImage(1, 0), asset_.GetImage(1, 1), asset_.GetImage(1, 0), asset_.GetImage(1, 1));
        btn_play_       = new ImageButton(this, true,  asset_.GetImage(2, 0), asset_.GetImage(2, 1), asset_.GetImage(2, 2), asset_.GetImage(2, 3));
        btn_forward_    = new ImageButton(this, false, asset_.GetImage(3, 0), asset_.GetImage(3, 1), asset_.GetImage(3, 0), asset_.GetImage(3, 1));
        btn_loop_       = new ImageButton(this, true,  asset_.GetImage(4, 0), asset_.GetImage(4, 1), asset_.GetImage(4, 2), asset_.GetImage(4, 3));
        //btn_metronome_  = new ImageButton(this, true,  asset_.GetImage(5, 0), asset_.GetImage(5, 1), asset_.GetImage(5, 2), asset_.GetImage(5, 3));
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        
        hbox->Add(btn_rewind_,      wxSizerFlags(0).Border(wxTOP|wxBOTTOM|wxRIGHT, 1));
        hbox->Add(btn_stop_,        wxSizerFlags(0).Border(wxTOP|wxBOTTOM|wxRIGHT, 1));
        hbox->Add(btn_play_,        wxSizerFlags(0).Border(wxTOP|wxBOTTOM|wxRIGHT, 1));
        hbox->Add(btn_forward_,     wxSizerFlags(0).Border(wxTOP|wxBOTTOM|wxRIGHT, 1));
        hbox->Add(btn_loop_,        wxSizerFlags(0).Border(wxTOP|wxBOTTOM|wxRIGHT, 1));
        //hbox->Add(btn_metronome_,   wxSizerFlags(0));
        hbox->AddStretchSpacer(1);
        
        SetSizer(hbox);
        
        SetBackgroundColour(wxColour(0x1B, 0x1B, 0x1B));
        
        btn_rewind_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnRewind(); });
        btn_stop_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnStop(); });
        btn_play_->Bind(wxEVT_TOGGLEBUTTON, [this](auto &ev) { OnPlay(); });
        btn_forward_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnForward(); });
        btn_loop_->Bind(wxEVT_TOGGLEBUTTON, [this](auto &ev) { OnLoop(); });
        //btn_metronome_->Bind(wxEVT_TOGGLEBUTTON, [this](auto &ev) { OnMetronome(); });
        
        slr_change_project_.reset(App::GetInstance()->GetChangeProjectListeners(), this);
        
        auto pj = Project::GetCurrentProject();
        assert(pj);
        
        auto &tp = pj->GetTransporter();
        slr_transporter_.reset(tp.GetListeners(), this);
        
        btn_play_->SetPushed(tp.IsPlaying());
        btn_loop_->SetPushed(tp.IsLoopEnabled());
    }
    
    ~TransportPanel()
    {}

private:
    ImageAsset      asset_;
    ImageButton     *btn_rewind_;
    ImageButton     *btn_stop_;
    ImageButton     *btn_play_;
    ImageButton     *btn_forward_;
    ImageButton     *btn_loop_;
    ImageButton     *btn_metronome_;
    ScopedListenerRegister<App::ChangeProjectListener> slr_change_project_;
    ScopedListenerRegister<Transporter::ITransportStateListener> slr_transporter_;
    
    void OnRewind()
    {
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        
        auto &tp = pj->GetTransporter();
        tp.Rewind();
    }
    
    void OnStop()
    {
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        
        auto &tp = pj->GetTransporter();
        tp.SetStop();
    }
    
    void OnPlay()
    {
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        
        auto &tp = pj->GetTransporter();
        tp.SetPlaying(tp.IsPlaying() == false);
    }
    
    void OnForward()
    {
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        
        auto &tp = pj->GetTransporter();
        tp.FastForward();
    }
    
    void OnLoop()
    {
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        
        auto &tp = pj->GetTransporter();
        tp.SetLoopEnabled(btn_loop_->IsPushed());
    }
    
    void OnMetronome()
    {
//        auto pj = Project::GetActiveProject();
//        pj->SetMetronome(btn_metronome_->GetValue());;
    }
    
    void OnChangeCurrentProject(Project *old_pj, Project *new_pj) override
    {
        if(old_pj) {
            slr_transporter_.reset();
        }
        
        if(new_pj) {
            slr_transporter_.reset(new_pj->GetTransporter().GetListeners(), this);
        }
    }
    
    void OnChanged(TransportInfo const &old_state,
                   TransportInfo const &new_state) override
    {
        auto differ = [&](auto const member) {
            return old_state.*member != new_state.*member;
        };
        
        if(differ(&TransportInfo::playing_)) {
            btn_play_->SetPushed(new_state.playing_);
            btn_play_->Refresh();
        }
        
        if(differ(&TransportInfo::loop_enabled_)) {
            btn_loop_->SetPushed(new_state.loop_enabled_);
            btn_loop_->Refresh();
        }
    }
};

class TimeIndicator
:   public wxPanel
,   public App::ChangeProjectListener
,   public Transporter::ITransportStateListener
{
public:
    TimeIndicator(wxWindow *parent, wxPoint pos, wxSize size)
    :   wxPanel(parent, wxID_ANY, pos, size)
    {
        SetDoubleBuffered(true);
        timer_.Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
        timer_.Start(kIntervalSlow);

        text_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL|wxST_NO_AUTORESIZE);
        
#if defined(_MSC_VER)
		auto font = wxFont(wxFontInfo(22).Family(wxFONTFAMILY_MODERN).FaceName("Tahoma"));
#else
        auto font = wxFont(wxFontInfo(26).Family(wxFONTFAMILY_MODERN).FaceName("Geneva"));
#endif
        text_->SetFont(font);
        text_->SetForegroundColour(wxColour(0xCB, 0xCB, 0xCB));
        UpdateTime(MBT(0, 0, 0));
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->AddStretchSpacer(1);
        vbox->Add(text_, wxSizerFlags(0).Expand());
        vbox->AddStretchSpacer(1);
        SetSizer(vbox);
        
        Layout();
        
        slr_change_project_.reset(App::GetInstance()->GetChangeProjectListeners(), this);
        
        auto pj = Project::GetCurrentProject();
        assert(pj);
        
        auto &tp = pj->GetTransporter();
        slr_transporter_.reset(tp.GetListeners(), this);
        
        SetBackgroundColour(wxColour(0x3B, 0x3B, 0x3B));
    }
    
    ~TimeIndicator()
    {}
    
private:
    UInt32 kIntervalSlow = 200;
    UInt32 kIntervalFast = 16;
    
    wxTimer timer_;
    TransportInfo last_info_;
    wxStaticText *text_;
    ScopedListenerRegister<App::ChangeProjectListener> slr_change_project_;
    ScopedListenerRegister<Transporter::ITransportStateListener> slr_transporter_;

    void OnChangeCurrentProject(Project *old_pj, Project *new_pj) override
    {
        if(old_pj) {
            slr_transporter_.reset();
        }
        
        if(new_pj) {
            slr_transporter_.reset(new_pj->GetTransporter().GetListeners(), this);
        }
    }
    
    void UpdateTime(MBT mbt)
    {
        text_->SetLabel("{:03d}:{:02d}:{:03d}"_format(mbt.measure_ + 1,
                                                      mbt.beat_ + 1,
                                                      mbt.tick_));
        Layout();
    }
        
    void OnChanged(TransportInfo const &old_state,
                   TransportInfo const &new_state) override
    {
        auto to_tuple = [](TransportInfo const &info) {
            return std::tie(info.play_.begin_, info.meter_);
        };
        if(to_tuple(old_state) == to_tuple(new_state)) {
            return;
        }
        
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        auto mbt = pj->TickToMBT(new_state.play_.begin_.tick_);
        UpdateTime(mbt);
    }
    
    void OnTimer()
    {
        auto pj = Project::GetCurrentProject();
        if(!pj) { return; }
        
        auto &tp = pj->GetTransporter();
        
        if(tp.IsPlaying() && timer_.GetInterval() == kIntervalSlow) {
            timer_.Start(kIntervalFast);
        } else if(tp.IsPlaying() == false && timer_.GetInterval() == kIntervalFast) {
            timer_.Start(kIntervalSlow);
        }
        
        auto new_info = tp.GetCurrentState();
        OnChanged(last_info_, new_info);
        last_info_ = new_info;
    }
};

class HeaderPanel
:   public wxPanel
{
public:
    HeaderPanel(wxWindow *parent)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    ,   col_bg_(10, 10, 10)
    {
        transport_buttons_ = new TransportPanel(this);
        time_indicator_ = new TimeIndicator(this, wxDefaultPosition, wxSize(220, 38));
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(transport_buttons_, wxSizerFlags(0).Expand());
        hbox->Add(time_indicator_, wxSizerFlags(0).Expand());
        hbox->AddStretchSpacer();
        
        SetSizer(hbox);
        
        SetBackgroundColour(col_bg_);
    }
    
private:
    wxPanel *transport_buttons_;
    wxPanel *time_indicator_;
    wxColor col_bg_;
};

class MyPanel
:   public wxPanel
,   public SingleInstance<MyPanel>
,   public PluginScanner::Listener
{
public:
    MyPanel(wxWindow *parent, wxSize size)
    : wxPanel(parent)
    {
        this->SetBackgroundColour(wxColour(0x09, 0x21, 0x33));
        
        header_panel_ = new HeaderPanel(this);
        
        auto pj = Project::GetCurrentProject();
        graph_panel_ = CreateGraphEditorComponent(this, pj->GetGraph()).release();
        graph_panel_->Show();
        
        pianoroll_ = CreatePianoRollWindow(this);
        pianoroll_->SetSize(size);
        pianoroll_->Hide();
        
        keyboard_ = CreateVirtualKeyboard(this);
  
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->Add(header_panel_, wxSizerFlags(0).Expand());
        vbox->Add(graph_panel_, wxSizerFlags(1).Expand());
        vbox->Add(pianoroll_, wxSizerFlags(1).Expand());
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->AddStretchSpacer(1);
        hbox->Add(keyboard_, wxSizerFlags(100000).Expand());
        hbox->AddStretchSpacer(1);
        vbox->Add(hbox, wxSizerFlags(0).Expand());
        
        SetSizer(vbox);
        
        SetClientSize(size);
        graph_panel_->RearrangeNodes();
        
        IMainFrame::GetInstance()->Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { SwitchPianoRoll(ev); }, ID_View_ShowPianoRoll);
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
    }
    
    ~MyPanel()
    {
    }
    
private:
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        Draw(dc);
    }
    
    void Draw(wxDC &dc)
    {
        //dc.SetBrush(wxBrush(wxColour(0x09, 0x21, 0x33)));
        //dc.DrawRectangle(GetClientRect());
    }
    
    class ComponentData : public wxClientData
    {
    public:
        ComponentData(schema::PluginDescription const &desc)
        :   desc_(desc)
        {}
        
        schema::PluginDescription desc_;
    };
    
    void SwitchPianoRoll(wxCommandEvent &ev)
    {
        if(ev.IsChecked()) {
            graph_panel_->Hide();
            pianoroll_->Show();
            keyboard_->Disable();
            keyboard_->Hide();
        } else {
            graph_panel_->Show();
            pianoroll_->Hide();
            keyboard_->Enable();
            keyboard_->Show();
        }
        
        Layout();
    }
    
    class MainPanelPianoRollViewStatus
    :   public IPianoRollViewStatus
    {
        Int32 GetScrollPosition(wxOrientation ort) const override
        {
            return 0;
        }
        
        void SetScrollPosition(wxOrientation ort, Int32 pos) override
        {}
        
        //! Get zoom factor.
        /*! @return the zoom factor for the orientation.
         *  a value greater then 1.0 means zoom-in, less then 1.0 means zoom-out.
         *  the value always greater than 0.0.
         */
        float GetZoomFactor(wxOrientation ort) const override
        {
            return 1.0;
        }
        
        void SetZoomFactor(wxOrientation ort, float factor, int zooming_pos) override
        {}
    };
    
    wxWindow        *keyboard_ = nullptr;
    wxPanel         *header_panel_ = nullptr;
    GraphEditor     *graph_panel_ = nullptr;
    wxWindow        *pianoroll_ = nullptr;
    MainPanelPianoRollViewStatus pianoroll_view_status_;
};

class MyPanel;

IMainFrame::IMainFrame()
:   wxFrame(nullptr, wxID_ANY, "", wxDefaultPosition, wxDefaultSize)
{}

class MainFrame
:   public IMainFrame
,   App::ChangeProjectListener
{
public:
    MainFrame(wxSize initial_size);
    ~MainFrame();
private:
    bool Destroy() override;
    void OnExit();
    void OnAbout(wxCommandEvent& event);
    void OnPlay(wxCommandEvent& event);
    void OnTimer();
    
    void OnBeforeSaveProject(Project *pj, schema::Project &schema) override;
    void OnAfterLoadProject(Project *pj, schema::Project const &schema) override;
    
private:
    std::string msg_;
    wxTimer timer_;
    MyPanel *my_panel_;
    ScopedListenerRegister<App::ChangeProjectListener> slr_change_project_;
};

MainFrame::MainFrame(wxSize initial_size)
:   IMainFrame()
{
	SetClientSize(initial_size);
    SetTitle("Untitled");
    
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_File_New, "&New File\tCTRL-N", "New File");
    menuFile->Append(ID_File_Open, "&Open...\tCTRL-O", "Open File");
    menuFile->Append(ID_File_Save, "&Save\tCTRL-S", "Save File");
    menuFile->Append(ID_File_SaveAs, "&Save As\tCTRL-SHIFT-S", "Save File As");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    
    wxMenu *menuEdit = new wxMenu;
    menuEdit->Append(ID_Setting, "&Setting\tCTRL-,", "Open Setting Dialog");
    
    wxMenu *menuView = new wxMenu;
    menuView->AppendCheckItem(ID_View_ShowPianoRoll, "Show &Piano Roll\tCTRL-P", "Show Piano Roll");
    
    wxMenu *menuPlay = new wxMenu;
    menuPlay->Append(ID_Play, "&Play\tSPACE", "Start playback", wxITEM_CHECK);

    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);

    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    menuBar->Append( menuEdit, "&Edit" );
    menuBar->Append( menuView, "&View" );
    menuBar->Append( menuPlay, "&Play" );
    menuBar->Append( menuHelp, "&Help" );
    SetMenuBar( menuBar );
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnExit(); }, wxID_EXIT);
    //Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) { OnExit(); });
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { App::GetInstance()->OnFileNew(); }, ID_File_New);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { App::GetInstance()->OnFileOpen(); }, ID_File_Open);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { App::GetInstance()->OnFileSave(false, false); }, ID_File_Save);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { App::GetInstance()->OnFileSave(true, false); }, ID_File_SaveAs);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { App::GetInstance()->ShowSettingDialog(); }, ID_Setting);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnPlay(ev); }, ID_Play);
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnAbout(ev); }, wxID_ABOUT);
    
    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    timer_.Start(1000);
    
    my_panel_ = new MyPanel(this, GetClientSize());
    
    slr_change_project_.reset(App::GetInstance()->GetChangeProjectListeners(), this);
    
    PCKeyboardInput::GetInstance()->ApplyTo(this);
}

MainFrame::~MainFrame()
{
}

bool MainFrame::Destroy()
{
    App::GetInstance()->BeforeExit();
    RemoveChild(my_panel_);
    my_panel_->Destroy();
    return wxFrame::Destroy();
}

void MainFrame::OnExit()
{
    auto app = App::GetInstance();
    auto saved = app->OnFileSave(false, true);
    if(!saved) { return; }
    
    Close(false);
}

void MainFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox(kAppName,
                 "created by hotwatermorning@gmail.com", wxOK | wxICON_INFORMATION );
}

void MainFrame::OnPlay(wxCommandEvent &ev)
{
    auto &tp = Project::GetCurrentProject()->GetTransporter();
    tp.SetPlaying(ev.IsChecked());
}

void MainFrame::OnTimer()
{
}

void MainFrame::OnBeforeSaveProject(Project *pj, schema::Project &schema)
{
    auto schema_rect = schema.mutable_frame_rect();
    auto rect = GetScreenRect();
    
    auto schema_pos = schema_rect->mutable_pos();
    schema_pos->set_x(rect.GetX());
    schema_pos->set_y(rect.GetY());
    
    auto schema_size = schema_rect->mutable_size();
    schema_size->set_width(rect.GetWidth());
    schema_size->set_height(rect.GetHeight());
}

void MainFrame::OnAfterLoadProject(Project *pj, schema::Project const &schema)
{
    wxRect rc;
    if(schema.has_frame_rect()) {
        auto const &rect = schema.frame_rect();
        if(rect.has_pos()) {
            auto const &pos = rect.pos();
            rc.SetPosition(wxPoint{pos.x(), pos.y()});
        }
        if(rect.has_size()) {
            auto const &size = rect.size();
            rc.SetSize(wxSize{size.width(), size.height()});
        }
    }
    
    wxDisplay disp{};
    auto client = disp.GetClientArea();

    int menu_height = 0;
    
#if defined(_MSC_VER)
    // do nothing
#else
    menu_height = GetMenuBarHeight();
#endif

    // constrain
    rc.SetWidth(Clamp<int>(rc.GetWidth(), GetMinWidth(), GetMaxWidth()));
    rc.SetHeight(Clamp<int>(rc.GetHeight(), GetMinHeight(), GetMaxHeight()));
    rc.SetX(Clamp<int>(rc.GetX(), 0, client.GetWidth()-100));
    rc.SetY(Clamp<int>(rc.GetY(), menu_height, client.GetHeight()-100));
    
    auto origin = GetClientAreaOrigin();
    rc.Offset(origin);
    
    SetSize(rc);
}

IMainFrame * CreateMainFrame(wxSize initial_size)
{
    return new MainFrame(initial_size);
}

NS_HWM_END
