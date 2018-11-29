#include "./GUI.hpp"
#include "../App.hpp"
#include "../project/Project.hpp"

#include <wx/tglbtn.h>
#include <wx/stdpaths.h>

#include <vector>

#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include "../misc/StrCnv.hpp"
#include "../plugin/PluginScanner.hpp"
#include "./PluginEditor.hpp"
#include "./Keyboard.hpp"

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
,   MyApp::ProjectActivationListener
,   Transporter::ITransportStateListener
{
    static
    String GetImagePath(String filename)
    {
        return wxStandardPaths::Get().GetResourcesDir() + L"/transport/" + filename;
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
        
        MyApp::GetInstance()->AddProjectActivationListener(this);
        
        auto pj = Project::GetInstance();
        auto &tp = pj->GetTransporter();
        
        tp.AddListener(this);
        
        btn_play_->SetPushed(tp.IsPlaying());
        btn_loop_->SetPushed(tp.IsLoopEnabled());
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
        auto pj = Project::GetActiveProject();
        auto &tp = pj->GetTransporter();
        tp.Rewind();
    }
    
    void OnStop()
    {
        auto pj = Project::GetActiveProject();
        auto &tp = pj->GetTransporter();
        tp.SetStop();
    }
    
    void OnPlay()
    {
        auto pj = Project::GetActiveProject();
        auto &tp = pj->GetTransporter();
        tp.SetPlaying(tp.IsPlaying() == false);
    }
    
    void OnForward()
    {
        auto pj = Project::GetActiveProject();
        auto &tp = pj->GetTransporter();
        tp.FastForward();
    }
    
    void OnLoop()
    {
        auto pj = Project::GetActiveProject();
        auto &tp = pj->GetTransporter();
        tp.SetLoopEnabled(btn_loop_->IsPushed());
    }
    
    void OnMetronome()
    {
//        auto pj = Project::GetActiveProject();
//        pj->SetMetronome(btn_metronome_->GetValue());;
    }
    
    void OnBeforeProjectDeactivated(Project *pj) override
    {
        auto &tp = pj->GetTransporter();
        tp.RemoveListener(this);
        
        MyApp::GetInstance()->RemoveProjectActivationListener(this);
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
,   public MyApp::ProjectActivationListener
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
        
        MyApp::GetInstance()->AddProjectActivationListener(this);
        
        auto pj = Project::GetInstance();
        assert(pj);
        
        auto &tp = pj->GetTransporter();
        tp.AddListener(this);
        
        SetBackgroundColour(wxColour(0x3B, 0x3B, 0x3B));
    }
    
private:
    UInt32 kIntervalSlow = 200;
    UInt32 kIntervalFast = 16;
    
    wxTimer timer_;
    TransportInfo last_info_;
    wxStaticText *text_;
    
    void OnBeforeProjectDeactivated(Project *pj) override
    {
        auto &tp = pj->GetTransporter();
        tp.RemoveListener(this);
        
        MyApp::GetInstance()->RemoveProjectActivationListener(this);
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
        auto pj = Project::GetInstance();
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
,   public MyApp::Vst3PluginLoadListener
,   public PluginScanner::Listener
{
public:
    MyPanel(wxWindow *parent)
    : wxPanel(parent)
    {
        this->SetBackgroundColour(wxColour(0x09, 0x21, 0x33));
        
        header_panel_ = new HeaderPanel(this);

        cho_select_component_ = new wxChoice(this, 102, wxDefaultPosition, wxSize(100, 20), 0, 0, wxCB_SORT);
        cho_select_component_->Show();
        
        tc_plugin_info_ = new wxTextCtrl(this, 103, "",
                                         wxDefaultPosition, wxSize(100, 100),
                                         wxTE_READONLY|wxTE_MULTILINE|wxTE_DONTWRAP);
        tc_plugin_info_->SetBackgroundColour(*wxWHITE);
        tc_plugin_info_->SetFont(wxFontInfo(12).Family(wxFONTFAMILY_MODERN));
        tc_plugin_info_->Hide();
        
        cho_select_unit_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(100, 20));
        cho_select_unit_->Hide();
        
        cho_select_program_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(100, 20));
        cho_select_program_->Hide();
        
        btn_open_editor_ = new wxButton(this, wxID_ANY, "Open Editor", wxDefaultPosition, wxSize(100, 20));
        btn_open_editor_->Hide();
        
        keyboard_ = CreateVirtualKeyboard(this);
  
        auto vbox = new wxBoxSizer(wxVERTICAL);
        vbox->Add(header_panel_, wxSizerFlags(0).Expand());
        vbox->Add(cho_select_component_, wxSizerFlags(0).Expand().Border(wxALL, 2));
        vbox->Add(tc_plugin_info_, wxSizerFlags(5).Expand().Border(wxALL, 2));
        vbox->Add(btn_open_editor_, wxSizerFlags(0).Border(wxALL, 2));
        unit_param_box_ = new wxStaticBoxSizer(wxVERTICAL, this, "Units && Programs");
        unit_param_box_->GetStaticBox()->Hide();
        unit_param_box_->GetStaticBox()->SetForegroundColour(wxColour(0xFF, 0xFF, 0xFF));
        unit_param_box_->Add(cho_select_unit_, wxSizerFlags(0).Expand().Border(wxBOTTOM|wxTOP, 2));
        unit_param_box_->Add(cho_select_program_, wxSizerFlags(0).Expand().Border(wxBOTTOM|wxTOP, 2));
        vbox->Add(unit_param_box_, wxSizerFlags(0).Expand().Border(wxALL, 2));
        vbox->AddStretchSpacer(1);
        vbox->Add(keyboard_, wxSizerFlags(0).Expand());
        
        SetSizerAndFit(vbox);
        
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { keyboard_->HandleWindowEvent(ev); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { keyboard_->HandleWindowEvent(ev); });
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        cho_select_component_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectComponent(); });
        btn_open_editor_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnOpenEditor(); });
        cho_select_unit_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectUnit(); });
        cho_select_program_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectProgram(); });
        
        MyApp::GetInstance()->AddVst3PluginLoadListener(this);
        PluginScanner::GetInstance()->AddListener(this);
        
        UpdateComponentList();
    }
    
    ~MyPanel()
    {
        if(editor_frame_) { editor_frame_->Destroy(); }
        PluginScanner::GetInstance()->RemoveListener(this);
        MyApp::GetInstance()->RemoveVst3PluginLoadListener(this);
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
    
    class UnitData : public wxClientData
    {
    public:
        UnitData(Steinberg::Vst::UnitID unit_id)
        :   unit_id_(unit_id)
        {}
        
        Steinberg::Vst::UnitID unit_id_ = -1;
    };
    
    void OnAfterVst3PluginLoaded(Vst3Plugin *plugin) override
    {
        auto factory_info = plugin->GetFactoryInfo();
        auto class_info = plugin->GetComponentInfo();
        
        auto str = (L"[Plugin Name]\n"
                    L"{}\n"
                    L"\n[Factory Info]\n"
                    L"Vendor: {}\n"
                    L"URL: {}\n"
                    L"Email: {}\n"
                    L"\n[Component Info]\n"
                    L"ID: {}\n"
                    L"Name: {}\n"
                    L"Category: {}\n"
                    L"Cardinality: {}"
                    L""_format(plugin->GetEffectName(),
                               factory_info.vendor(),
                               factory_info.url(),
                               factory_info.email(),
                               to_wstr(std::string{ class_info.cid().begin(), class_info.cid().end() }),
                               class_info.name(),
                               class_info.category(),
                               class_info.cardinality())
                    );
        
        if(class_info.has_classinfo2()) {
            auto &ci2 = class_info.classinfo2();
            str += (L"\n"
                    L"\n[Additional Component Info]\n"
                    L"Sub Categories: {}\n"
                    L"Vendor: {}\n"
                    L"Version: {}\n"
                    L"SDK Version: {}"
                    L""_format(ci2.sub_categories(),
                               ci2.vendor(),
                               ci2.version(),
                               ci2.sdk_version())
                    );
        }

        tc_plugin_info_->Clear();
        *tc_plugin_info_ << str;
        tc_plugin_info_->ShowPosition(0);
        
        cho_select_unit_->Clear();
        cho_select_program_->Clear();
        
        auto const num = plugin->GetNumUnitInfo();
        for(int un = 0; un < num; ++un) {
            auto const &info = plugin->GetUnitInfoByIndex(un);
            auto const &pl = info.program_list_;
            if(info.program_change_param_ == Steinberg::Vst::kNoParamId) { continue; }
            if(pl.id_ == Steinberg::Vst::kNoProgramListId) { continue; }
            if(pl.programs_.empty()) { continue; }
            
            cho_select_unit_->Append(info.name_, new UnitData{info.id_});
        }
        
        if(cho_select_unit_->GetCount() == 0) {
            // nothing to do for cho_select_unit_ and cho_select_program_
        } else if(cho_select_unit_->GetCount() == 1) {
            unit_param_box_->GetStaticBox()->Show();
            cho_select_unit_->SetSelection(0);
            cho_select_unit_->Show();
            cho_select_unit_->Disable();
            OnSelectUnit();
        } else {
            unit_param_box_->GetStaticBox()->Show();
            cho_select_unit_->SetSelection(0);
            cho_select_unit_->Show();
            cho_select_unit_->Enable();
            OnSelectUnit();
        }
        
        tc_plugin_info_->Show();
        btn_open_editor_->Show();
        btn_open_editor_->Enable();
        
        Layout();
    }
    
    void OnBeforeVst3PluginUnloaded(Vst3Plugin *plugin) override
    {
        tc_plugin_info_->Hide();
        btn_open_editor_->Hide();
        cho_select_unit_->Hide();
        cho_select_program_->Hide();
        unit_param_box_->GetStaticBox()->Hide();
    }
    
    void OnScanningProgressUpdated(PluginScanner *) override {
        CallAfter([this] { UpdateComponentList(); });
    }
    
    void OnScanningFinished(PluginScanner *) override {
        CallAfter([this] { UpdateComponentList(); });
    }
    
    void UpdateComponentList()
    {
        auto ps = PluginScanner::GetInstance();
        auto descs = ps->GetPluginDescriptions();
        
        auto const contains = [](PluginDescription const &desc, std::string const &str) {
            if(desc.vst3info().has_classinfo2()) {
                return desc.vst3info().classinfo2().subcategories().find(str) != std::string::npos;
            } else {
                return false;
            }
        };
        
        cho_select_component_->Clear();
        for(auto const &desc: descs) {
            bool const is_inst = contains(desc, "Instrument");
            bool const is_fx = contains(desc, "Fx");
            
            std::wstring type;
            if(is_inst && is_fx) { type = L"[Inst|Fx]"; }
            else if(is_inst) { type = L"[Inst]"; }
            else if(is_fx) { type = L"[Fx]"; }
            else { type = L"[Unknown]"; }
            
            cho_select_component_->Append(type + L" " + to_wstr(desc.name()), new ComponentData(desc));
        }
        cho_select_component_->SetSelection(wxNOT_FOUND);
    }
    
    void OnSelectComponent() {
        auto sel = cho_select_component_->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto const p = static_cast<ComponentData const *>(cho_select_component_->GetClientObject(sel));
        MyApp::GetInstance()->LoadVst3Plugin(p->desc_);
    }
    
    Vst3Plugin::UnitID GetCurrentUnitID() const
    {
        auto const sel = cho_select_unit_->GetSelection();
        if(sel == wxNOT_FOUND) { return -1; }
        
        assert(cho_select_unit_->HasClientObjectData());
        auto data = static_cast<UnitData *>(cho_select_unit_->GetClientObject(sel));
        return data->unit_id_;
    }
    
    void OnOpenEditor()
    {
        if(editor_frame_) { return; }
        
        editor_frame_ = CreatePluginEditorFrame(this,
                                                MyApp::GetInstance()->GetVst3Plugin(),
                                                [this] {
                                                    editor_frame_ = nullptr;
                                                    btn_open_editor_->Enable();
                                                });
        btn_open_editor_->Disable();
    }
    
    void OnSelectUnit()
    {
        auto sel = cho_select_unit_->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto unit_id = GetCurrentUnitID();
        assert(unit_id != -1);
        
        auto plugin = MyApp::GetInstance()->GetVst3Plugin();
        auto info = plugin->GetUnitInfoByID(unit_id);
        
        assert(info.program_list_.programs_.size() >= 1);

        // update program list
        cho_select_program_->Clear();
        for(UInt32 i = 0; i < info.program_list_.programs_.size(); ++i) {
            cho_select_program_->Append(info.program_list_.programs_[i].name_);
        }
        
        cho_select_program_->Select(plugin->GetProgramIndex(unit_id));
        cho_select_program_->Show();
        Layout();
    }
    
    void OnSelectProgram()
    {
        auto sel = cho_select_program_->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        assert(cho_select_unit_->GetSelection() != wxNOT_FOUND);
        
        auto const unit_id = GetCurrentUnitID();
        assert(unit_id != -1);
        
        auto plugin = MyApp::GetInstance()->GetVst3Plugin();
        auto info = plugin->GetUnitInfoByID(unit_id);
        
        plugin->SetProgramIndex(sel, unit_id);
    }
    
    wxStaticBoxSizer *unit_param_box_;
    wxChoice        *cho_select_component_;
    wxTextCtrl      *tc_plugin_info_;
    wxChoice        *cho_select_unit_;
    wxChoice        *cho_select_program_;
    wxButton        *btn_open_editor_;
    wxPanel         *keyboard_;
    wxFrame         *editor_frame_ = nullptr;
    wxPanel         *header_panel_ = nullptr;
};

enum
{
    ID_Play = 1,
    ID_EnableInputs,
    ID_RescanPlugin,
    ID_ForceRescanPlugin,
};

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
: wxFrame(NULL, wxID_ANY, title, pos, size)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(ID_RescanPlugin, "&Rescan Plugins", "Rescan Plugins");
    menuFile->Append(ID_ForceRescanPlugin, "&Clear and Rescan Plugins", "Clear and Rescan Plugins");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    
    wxMenu *menuPlay = new wxMenu;
    menuPlay->Append(ID_Play, "&Play\tCtrl-P", "Start playback", wxITEM_CHECK);
    menuPlay->Append(ID_EnableInputs,
                     "&Enable Mic Inputs\tCtrl-I",
                     "Input Mic Inputs",
                     wxITEM_CHECK)
    ->Enable(Project::GetInstance()->CanInputsEnabled());

    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);

    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    menuBar->Append( menuPlay, "&Play" );
    menuBar->Append( menuHelp, "&Help" );
    SetMenuBar( menuBar );
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnExit(); }, wxID_EXIT);
    //Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) { OnExit(); });
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { MyApp::GetInstance()->RescanPlugins(); }, ID_RescanPlugin);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { MyApp::GetInstance()->ForceRescanPlugins(); }, ID_ForceRescanPlugin);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnPlay(ev); }, ID_Play);
    Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) { OnEnableInputs(ev); }, ID_EnableInputs);
    
    Bind(wxEVT_MENU, [this](auto &ev) { OnAbout(ev); }, wxID_ABOUT);
    
    timer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    timer_.Start(1000);
    
    my_panel_ = new MyPanel(this);
    my_panel_->SetSize(GetClientSize());
}

MyFrame::~MyFrame()
{
    int x = 0;
    x = 1;
}

bool MyFrame::Destroy()
{
    MyApp::GetInstance()->BeforeExit();
    //my_panel_->Destroy();
    return wxFrame::Destroy();
}

void MyFrame::OnExit()
{
    //MyApp::GetInstance()->BeforeExit();
    Close( true );
}

void MyFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox( "VST3HostDemo",
                 "created by hotwatermorning@gmail.com", wxOK | wxICON_INFORMATION );
}

void MyFrame::OnPlay(wxCommandEvent &ev)
{
    auto &tp = Project::GetInstance()->GetTransporter();
    tp.SetPlaying(ev.IsChecked());
}

void MyFrame::OnEnableInputs(wxCommandEvent &ev)
{
    auto *pj = Project::GetInstance();
    pj->SetInputsEnabled(ev.IsChecked());
}

void MyFrame::OnTimer()
{
}

NS_HWM_END
