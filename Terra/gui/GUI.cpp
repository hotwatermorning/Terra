#include "./GUI.hpp"
#include "../App.hpp"
#include "../project/Project.hpp"

#include <wx/tglbtn.h>
#include <wx/stdpaths.h>
#include <wx/display.h>

#include <vector>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../misc/StrCnv.hpp"
#include "../misc/MathUtil.hpp"
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
        
        slr_change_project_.reset(MyApp::GetInstance()->GetChangeProjectListeners(), this);
        
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
    ScopedListenerRegister<MyApp::ChangeProjectListener> slr_change_project_;
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
        UpdateTime(MBT(0, 0, 0));
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->AddStretchSpacer(1);
        vbox->Add(text_, wxSizerFlags(0).Expand());
        vbox->AddStretchSpacer(1);
        SetSizer(vbox);
        
        Layout();
        
        slr_change_project_.reset(MyApp::GetInstance()->GetChangeProjectListeners(), this);
        
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
    ScopedListenerRegister<MyApp::ChangeProjectListener> slr_change_project_;
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

Sequence MakeSequence(std::vector<Int8> pitches);

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
        
        timer_.Bind(wxEVT_TIMER, [this, i = 0](auto &ev) mutable {
            auto pj = Project::GetCurrentProject();
            if(!pj) { return; }
//            if(i % 2 == 0) {
//                pj->GetSequence() = MakeSequence({48, 50, 51, 55, 58});
//            } else {
//                pj->GetSequence() = MakeSequence({48 + 24, 50 + 24, 51 + 24, 55 + 24, 58 + 24});
//            }
            pj->CacheSequence();
            ++i;
        });
        timer_.Start(10);
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
        ComponentData(schema::PluginDescription const &desc)
        :   desc_(desc)
        {}
        
        schema::PluginDescription desc_;
    };
    
    wxPanel         *keyboard_;
    wxPanel         *header_panel_ = nullptr;
    GraphEditor     *graph_panel_ = nullptr;
    
    wxTimer timer_;
};

class MyPanel;

class MyFrame
:   public wxFrame
,   SingleInstance<MyFrame>
,   MyApp::ChangeProjectListener
{
public:
    MyFrame();
    ~MyFrame();
private:
    bool Destroy() override;
    void OnExit();
    void OnAbout(wxCommandEvent& event);
    void OnPlay(wxCommandEvent& event);
    void OnEnableInputs(wxCommandEvent& event);
    void OnTimer();
    
    void OnBeforeSaveProject(Project *pj, schema::Project &schema) override;
    void OnAfterLoadProject(Project *pj, schema::Project const &schema) override;
    
private:
    std::string msg_;
    wxTimer timer_;
    MyPanel *my_panel_;
    ScopedListenerRegister<MyApp::ChangeProjectListener> slr_change_project_;
};

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
};

MyFrame::MyFrame()
: wxFrame(nullptr, wxID_ANY, "Untitled", wxDefaultPosition, wxDefaultSize)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_File_New, "&New File\tCTRL-N", "New File");
    menuFile->Append(ID_File_Open, "&Open...\tCTRL-O", "Open File");
    menuFile->Append(ID_File_Save, "&Save\tCTRL-S", "Save File");
    menuFile->Append(ID_File_SaveAs, "&Save As\tCTRL-SHIFT-S", "Save File As");
    menuFile->AppendSeparator();
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
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->OnFileNew(); }, ID_File_New);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->OnFileOpen(); }, ID_File_Open);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->OnFileSave(false, false); }, ID_File_Save);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->OnFileSave(true, false); }, ID_File_SaveAs);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->RescanPlugins(); }, ID_RescanPlugin);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->ForceRescanPlugins(); }, ID_ForceRescanPlugin);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [](auto &ev) { MyApp::GetInstance()->ShowSettingDialog(); }, ID_Setting);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnPlay(ev); }, ID_Play);
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnAbout(ev); }, wxID_ABOUT);
    
    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    timer_.Start(1000);
    
    my_panel_ = new MyPanel(this, GetClientSize());
    
    slr_change_project_.reset(MyApp::GetInstance()->GetChangeProjectListeners(), this);
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
    auto app = MyApp::GetInstance();
    auto saved = app->OnFileSave(false, true);
    if(!saved) { return; }
    
    Close(false);
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

void MyFrame::OnBeforeSaveProject(Project *pj, schema::Project &schema)
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

UInt32 GetMenuBarHeight();

void MyFrame::OnAfterLoadProject(Project *pj, schema::Project const &schema)
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

wxFrame * CreateMainFrame()
{
    return new MyFrame();
}

NS_HWM_END
