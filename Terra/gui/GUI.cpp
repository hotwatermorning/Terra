#include "./GUI.hpp"
#include "../App.hpp"
#include "../project/Project.hpp"

#include <wx/tglbtn.h>

#include <vector>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../misc/StrCnv.hpp"
#include "../plugin/PluginScanner.hpp"
#include "./PluginEditor.hpp"
#include "./Keyboard.hpp"
#include "./UnitData.hpp"
#include "./GraphEditor.hpp"
#include "../resource/ResourceHelper.hpp"

NS_HWM_BEGIN

bool IsPushed(wxButton const *btn)
{
    return false;
}

bool IsPushed(wxToggleButton const *btn)
{
    return btn->GetValue();
}

class ImageButton
:   public wxWindow
{
public:
    ImageButton(wxWindow *parent,
                bool is_3state,
                wxImage normal,
                wxImage hover,
                wxImage pushed,
                wxImage hover_pushed,
                wxPoint pos = wxDefaultPosition,
                wxSize size = wxDefaultSize)
    :   wxWindow(parent, wxID_ANY, pos, size)
    ,   normal_(normal)
    ,   hover_(hover)
    ,   pushed_(pushed)
    ,   hover_pushed_(hover_pushed)
    ,   is_3state_(is_3state)
    {
        SetMinSize(normal.GetSize());
        SetSize(normal.GetSize());
        Bind(wxEVT_ENTER_WINDOW, [this](auto &ev) {
            is_hover_ = true;
            Refresh();
        });
        
        Bind(wxEVT_LEAVE_WINDOW, [this](auto &ev) {
            is_hover_ = false;
            is_being_pressed_ = false;
            Refresh();
        });
        
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) {
            is_being_pressed_ = true;
            Refresh();
        });
        
        Bind(wxEVT_LEFT_DCLICK, [this](auto &ev) {
            is_being_pressed_ = true;
            Refresh();
        });
        
        Bind(wxEVT_LEFT_UP, [this](auto &ev) {            
            if(!is_hover_) { return; }
            if(is_3state_) {
                is_pushed_ = !is_pushed_;
            } else {
                is_pushed_ = false;
            }
            is_being_pressed_ = false;
            
            wxEventType type;
            if(is_3state_) {
                type = wxEVT_TOGGLEBUTTON;
            } else {
                type = wxEVT_BUTTON;
            }
            wxCommandEvent new_ev(type);
            wxPostEvent(this, new_ev);
            Refresh();
        });
        
        Bind(wxEVT_PAINT, [this](auto &ev) {
            OnPaint();
        });
    }
    
    bool IsPushed() const { return is_pushed_; }
    void SetPushed(bool status) { is_pushed_ = status; }
    
    void OnPaint()
    {
        wxImage img;
        if(is_being_pressed_) {
            img = pushed_;
        } else if(is_hover_) {
            if(is_pushed_) {
                img = hover_pushed_;
            } else {
                img = hover_;
            }
        } else if(is_pushed_) {
            img = pushed_;
        } else {
            img = normal_;
        }
        
        wxPaintDC dc(this);
        dc.DrawBitmap(img, 0, 0);
    }
    
private:
    wxImage normal_;
    wxImage hover_;
    wxImage pushed_;
    wxImage hover_pushed_;
    bool is_hover_ = false;
    bool is_3state_ = false;
    bool is_being_pressed_ = false;
    bool is_pushed_ = false;
};

class ImageAsset
{
public:
    ImageAsset()
    :   num_cols_(0)
    ,   num_rows_(0)
    {}
    
    ImageAsset(String filepath, int num_cols, int num_rows)
    :   num_cols_(num_cols)
    ,   num_rows_(num_rows)
    {
        assert(num_cols_ > 1);
        assert(num_rows_ > 1);
        
        image_ = wxImage(filepath);
        
        assert(image_.GetWidth() % num_cols_ == 0);
        assert(image_.GetHeight() % num_rows_ == 0);
    }
    
    wxImage GetImage(int col, int row) const
    {
        assert(0 <= col && col < num_cols_);
        assert(0 <= row && row < num_rows_);
        
        auto const w = image_.GetWidth() / num_cols_;
        auto const h = image_.GetHeight() / num_rows_;
        
        wxRect r(wxPoint(w * col, h * row), wxSize(w, h));
        return image_.GetSubImage(r);
    }
    
private:
    wxImage image_;
    int num_cols_;
    int num_rows_;
};

class TransportPanel
:   public wxPanel
,   MyApp::ChangeProjectListener
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
        
        MyApp::GetInstance()->AddChangeProjectListener(this);
        
        auto pj = Project::GetCurrentProject();
        assert(pj);
        
        auto &tp = pj->GetTransporter();
        tp.AddListener(this);
        
        btn_play_->SetPushed(tp.IsPlaying());
        btn_loop_->SetPushed(tp.IsLoopEnabled());
    }
    
    ~TransportPanel()
    {
        MyApp::GetInstance()->RemoveChangeProjectListener(this);
        
        auto pj = Project::GetCurrentProject();
        if(pj) {
            auto &tp = pj->GetTransporter();
            tp.RemoveListener(this);
        }
    }

private:
    ImageAsset      asset_;
    ImageButton     *btn_rewind_;
    ImageButton     *btn_stop_;
    ImageButton     *btn_play_;
    ImageButton     *btn_forward_;
    ImageButton     *btn_loop_;
    ImageButton     *btn_metronome_;
    
    void OnRewind()
    {
        auto pj = Project::GetCurrentProject();
        auto &tp = pj->GetTransporter();
        tp.Rewind();
    }
    
    void OnStop()
    {
        auto pj = Project::GetCurrentProject();
        auto &tp = pj->GetTransporter();
        tp.SetStop();
    }
    
    void OnPlay()
    {
        auto pj = Project::GetCurrentProject();
        auto &tp = pj->GetTransporter();
        tp.SetPlaying(tp.IsPlaying() == false);
    }
    
    void OnForward()
    {
        auto pj = Project::GetCurrentProject();
        auto &tp = pj->GetTransporter();
        tp.FastForward();
    }
    
    void OnLoop()
    {
        auto pj = Project::GetCurrentProject();
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
            old_pj->GetTransporter().RemoveListener(this);
        }
        
        if(new_pj) {
            new_pj->GetTransporter().AddListener(this);
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
,   public MyApp::ChangeProjectListener
,   public Transporter::ITransportStateListener
{
public:
    TimeIndicator(wxWindow *parent, wxPoint pos, wxSize size)
    :   wxPanel(parent, wxID_ANY, pos, size)
    {
        timer_.Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
        timer_.Start(kIntervalSlow);

        text_ = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
        
        // geneva, tahoma
        wxSize font_size(size.GetHeight() - 4, size.GetHeight() - 4);
        auto font = wxFont(wxFontInfo(font_size).Family(wxFONTFAMILY_MODERN).FaceName("Geneva"));
        text_->SetFont(font);
        text_->SetForegroundColour(wxColour(0xCB, 0xCB, 0xCB));
        text_->SetLabel("0000:00:000");
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->AddStretchSpacer(1);
        vbox->Add(text_, wxSizerFlags(0).Expand());
        vbox->AddStretchSpacer(1);
        SetSizer(vbox);
        
        Layout();
        
        MyApp::GetInstance()->AddChangeProjectListener(this);
        
        auto pj = Project::GetCurrentProject();
        assert(pj);
        
        auto &tp = pj->GetTransporter();
        tp.AddListener(this);
        
        SetBackgroundColour(wxColour(0x3B, 0x3B, 0x3B));
    }
    
    ~TimeIndicator()
    {
        MyApp::GetInstance()->RemoveChangeProjectListener(this);
        auto pj = Project::GetCurrentProject();
        if(pj) {
            auto &tp = pj->GetTransporter();
            tp.RemoveListener(this);
        }
    }
    
private:
    UInt32 kIntervalSlow = 200;
    UInt32 kIntervalFast = 16;
    
    wxTimer timer_;
    TransportInfo last_info_;
    wxStaticText *text_;

    void OnChangeCurrentProject(Project *old_pj, Project *new_pj) override
    {
        if(old_pj) {
            old_pj->GetTransporter().RemoveListener(this);
        }
        
        if(new_pj) {
            new_pj->GetTransporter().AddListener(this);
        }
    }
        
    void OnChanged(TransportInfo const &old_state,
                   TransportInfo const &new_state) override
    {
        auto to_tuple = [](TransportInfo const &info) {
            return std::tie(info.ppq_begin_pos_, info.time_sig_numer_, info.time_sig_denom_);
        };
        if(to_tuple(old_state) == to_tuple(new_state)) {
            return;
        }
        
        auto const kTpqn = 480;
        auto const tick = (Int64)(new_state.ppq_begin_pos_ * kTpqn);
        auto const beat_length = (kTpqn * 4) / new_state.time_sig_denom_;
        auto const measure_length = beat_length * new_state.time_sig_numer_;
        auto measure_pos = tick / measure_length;
        auto beat_pos = (tick % measure_length) / beat_length;
        auto tick_pos = tick % beat_length;
        
        text_->SetLabel("{:04d}:{:02d}:{:03d}"_format(measure_pos + 1,
                                                      beat_pos + 1,
                                                      tick_pos)
                        );
        Layout();
    }
    
    void OnTimer()
    {
        auto pj = Project::GetCurrentProject();
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
        
        keyboard_ = CreateVirtualKeyboard(this);
  
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->Add(header_panel_, wxSizerFlags(0).Expand());
        vbox->Add(graph_panel_, wxSizerFlags(1).Expand());
        vbox->Add(keyboard_, wxSizerFlags(0).Expand());
        
        SetSizer(vbox);
        
        SetSize(size);
        graph_panel_->RearrangeNodes();
        
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { keyboard_->HandleWindowEvent(ev); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { keyboard_->HandleWindowEvent(ev); });
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
    }
    
    ~MyPanel()
    {
    }
    
private:
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
        Draw(pdc);
    }
    
    void Draw(wxDC &dc)
    {
        //dc.SetBrush(wxBrush(wxColour(0x09, 0x21, 0x33)));
        //dc.DrawRectangle(GetClientRect());
    }
    
    class ComponentData : public wxClientData
    {
    public:
        ComponentData(PluginDescription const &desc)
        :   desc_(desc)
        {}
        
        PluginDescription desc_;
    };
    
    wxPanel         *keyboard_;
    wxPanel         *header_panel_ = nullptr;
    GraphEditor     *graph_panel_ = nullptr;
};

enum
{
    ID_Play = 1,
    ID_RescanPlugin,
    ID_ForceRescanPlugin,
    ID_Setting
};

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
: wxFrame(nullptr, wxID_ANY, title, pos, size)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_RescanPlugin, "&Rescan Plugins", "Rescan Plugins");
    menuFile->Append(ID_ForceRescanPlugin, "&Clear and Rescan Plugins", "Clear and Rescan Plugins");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    
    wxMenu *menuEdit = new wxMenu;
    menuEdit->Append(ID_Setting, "&Setting\tCTRL-,", "Open Setting Dialog");
    
    wxMenu *menuPlay = new wxMenu;
    menuPlay->Append(ID_Play, "&Play\tSPACE", "Start playback", wxITEM_CHECK);

    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);

    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    menuBar->Append( menuEdit, "&Edit" );
    menuBar->Append( menuPlay, "&Play" );
    menuBar->Append( menuHelp, "&Help" );
    SetMenuBar( menuBar );
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnExit(); }, wxID_EXIT);
    //Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) { OnExit(); });
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { MyApp::GetInstance()->RescanPlugins(); }, ID_RescanPlugin);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { MyApp::GetInstance()->ForceRescanPlugins(); }, ID_ForceRescanPlugin);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { MyApp::GetInstance()->ShowSettingDialog(); }, ID_Setting);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnPlay(ev); }, ID_Play);
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnAbout(ev); }, wxID_ABOUT);
    
    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    timer_.Start(1000);
    
    my_panel_ = new MyPanel(this, GetClientSize());
}

MyFrame::~MyFrame()
{
}

bool MyFrame::Destroy()
{
    MyApp::GetInstance()->BeforeExit();
    RemoveChild(my_panel_);
    my_panel_->Destroy();
    return wxFrame::Destroy();
}

void MyFrame::OnExit()
{
    //MyApp::GetInstance()->BeforeExit();
    Close( true );
}

void MyFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox(kAppName,
                 "created by hotwatermorning@gmail.com", wxOK | wxICON_INFORMATION );
}

void MyFrame::OnPlay(wxCommandEvent &ev)
{
    auto &tp = Project::GetCurrentProject()->GetTransporter();
    tp.SetPlaying(ev.IsChecked());
}

void MyFrame::OnTimer()
{
}

NS_HWM_END
