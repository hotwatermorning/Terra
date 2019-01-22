#include "SettingWindow.hpp"
#include "Util.hpp"

NS_HWM_BEGIN

class DeviceSettingPanel
:   public wxPanel
{
    BrushPen background = HSVToColour(0.4, 0.7, 0.9);
    wxPen title = HSVToColour(0.0, 0.0, 1.0);
 
public:
    DeviceSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        st_audio_inputs_ = new wxStaticText(this, wxID_ANY, "Audio Input: ");
        cho_audio_inputs_ = new wxChoice(this, wxID_ANY);
        st_audio_outputs_ = new wxStaticText(this, wxID_ANY, "Audio Output: ");
        cho_audio_outputs_ = new wxChoice(this, wxID_ANY);
        st_sample_rates_ = new wxStaticText(this, wxID_ANY, "Sample Rate: ");
        cho_sample_rates_ = new wxChoice(this, wxID_ANY);
        st_buffer_sizes_ = new wxStaticText(this, wxID_ANY, "Buffer Size: ");
        cho_buffer_sizes_ = new wxChoice(this, wxID_ANY);
        btn_apply_audio_ = new wxButton(this, wxID_ANY, "Apply");
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
    
        auto add_entry = [&](auto parent_box, auto static_text, auto choice) {
            auto hbox = new wxBoxSizer(wxHORIZONTAL);
            hbox->Add(static_text, wxSizerFlags(0).Expand());
            hbox->Add(choice, wxSizerFlags(1).Expand());
            parent_box->Add(hbox, wxSizerFlags(0).Expand());
        };
        
        add_entry(vbox, st_audio_inputs_, cho_audio_inputs_);
        add_entry(vbox, st_audio_outputs_, cho_audio_outputs_);
        add_entry(vbox, st_sample_rates_, cho_sample_rates_);
        add_entry(vbox, st_buffer_sizes_, cho_buffer_sizes_);
        
        {
            auto hbox = new wxBoxSizer(wxHORIZONTAL);
            hbox->AddStretchSpacer(1);
            hbox->Add(btn_apply_audio_, wxSizerFlags(0).Expand());
            vbox->Add(hbox, wxSizerFlags(0).Expand());
        }
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        cho_audio_inputs_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectAudioInput(ev); });
        cho_audio_outputs_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectAudioOutput(ev); });
        cho_sample_rates_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectSampleRate(ev); });
        cho_buffer_sizes_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectBufferSize(ev); });
        btn_apply_audio_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnPushApplyAudio(ev); });
    }
    
    void OnSelectAudioInput(wxCommandEvent const &ev)
    {
        
    }
    
    void OnSelectAudioOutput(wxCommandEvent const &ev)
    {
        
    }
    
    void OnSelectSampleRate(wxCommandEvent const &ev)
    {
        
    }
    
    void OnSelectBufferSize(wxCommandEvent const &ev)
    {
        
    }
    
    void OnPushApplyAudio(wxCommandEvent const &ev)
    {
    }
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        
        background.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
    
private:
    wxStaticText *st_audio_inputs_;
    wxChoice *cho_audio_inputs_;
    wxStaticText *st_audio_outputs_;
    wxChoice *cho_audio_outputs_;
    wxStaticText *st_sample_rates_;
    wxChoice *cho_sample_rates_;
    wxStaticText *st_buffer_sizes_;
    wxChoice *cho_buffer_sizes_;
    
    wxButton *btn_apply_audio_;
};

class AppearanceSettingPanel
:   public wxPanel
{
    BrushPen background = HSVToColour(0.6, 0.7, 0.9);
    
public:
    AppearanceSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
    }
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        
        background.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
};

class PluginSettingPanel
:   public wxPanel
{
    BrushPen background = HSVToColour(0.8, 0.7, 0.9);
    
public:
    PluginSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
    }
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        
        background.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
};

class TabPanel
:   public wxPanel
{
    BrushPen background = { HSVToColour(0.8, 0.3, 0.9) };
    
public:
    enum class TabID {
        kDevice,
        kAppearance,
        kPlugin,
        kNumIDs,
    };
    
    static
    int GetNumIDs() { return (int)TabID::kNumIDs; }
    
    struct Callback
    {
    protected:
        Callback() {}
    public:
        virtual ~Callback() {}
        virtual void OnSelectTab(TabID id) = 0;
    };
    
public:
    TabPanel(wxWindow *parent, Callback *callback)
    :   wxPanel(parent)
    ,   callback_(callback)
    {
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { OnKeyDown(ev); });
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
    }
    
    void OnLeftUp(wxMouseEvent &ev)
    {
        auto tab = GetTabFromPoint(ev.GetPosition());
        if(!tab) { return; }
        SetTab(*tab);
        callback_->OnSelectTab(*tab);
        Refresh();
    }
    
    void OnMouseMove(wxMouseEvent &ev)
    {
        
    }
    
    wxRect GetRectFromTab(TabID id) const
    {
        assert(0 <= (int)id && (int)id < GetNumIDs());
        
        auto this_size = GetSize();
        wxSize const size(this_size.GetHeight(), this_size.GetHeight()); // square
        wxPoint const pos((int)id * size.GetWidth(), 0);
        return wxRect(pos, size);
    }
    
    std::optional<TabID> GetTabFromPoint(wxPoint pt) const
    {
        for(int i = 0; i < GetNumIDs(); ++i) {
            auto rc = GetRectFromTab((TabID)i);
            if(rc.Contains(pt)) { return (TabID)i; }
        }
        
        return std::nullopt;
    }
    
    void OnPaint()
    {
        wxPaintDC dc(this);

        auto rect = GetClientRect();
        background.ApplyTo(dc);
        dc.DrawRectangle(rect);
        
        BrushPen icon_colour = { HSVToColour(0.0, 0.0, 0.6), HSVToColour(0.0, 0.0, 0.4) };
        wxPen title_colour = HSVToColour(0.0, 0.0, 0.2);
        
        auto get_tab_name = [](auto id) {
            if(id == TabID::kDevice) { return "Device"; }
            if(id == TabID::kAppearance) { return "Appearance"; }
            if(id == TabID::kPlugin) { return "Plugin"; }
            assert(false);
            return "unknown";
        };
        
        for(int i = 0; i < (int)TabID::kNumIDs; ++i) {
            icon_colour.ApplyTo(dc);
            auto rc = GetRectFromTab((TabID)i);
            dc.DrawRectangle(rc);
            dc.SetPen(title_colour);
            auto name = get_tab_name((TabID)i);
            dc.DrawLabel(name, rc, wxALIGN_CENTER);
        }
    }
    
    void OnKeyDown(wxKeyEvent &ev)
    {
        if(ev.GetUnicodeKey() != WXK_NONE) { return; }
        
        if(ev.GetKeyCode() == WXK_LEFT) {
            MoveToPrevTab();
        } else if(ev.GetKeyCode() == WXK_RIGHT) {
            MoveToNextTab();
        } else if(ev.GetKeyCode() == WXK_SHIFT) {
            bool forward = (ev.ShiftDown() == false);
            if(forward) { MoveToNextTab(); }
            else        { MoveToPrevTab(); }
        }
        Refresh();
    }
    
    void SetTab(TabID id)
    {
        current_tab_ = id;
        Refresh();
    }
    
    void MoveToNextTab()
    {
        current_tab_ = TabID(((int)current_tab_ + 1) % (int)TabID::kNumIDs);
        Refresh();
    }
    
    void MoveToPrevTab()
    {
        current_tab_ = TabID(((int)current_tab_ + (int)TabID::kNumIDs - 1) % (int)TabID::kNumIDs);
        Refresh();
    }
    
private:
    Callback *callback_;
    TabID current_tab_ = (TabID)0;
};

class SettingFrame
:   public wxFrame
,   TabPanel::Callback
{
    wxRect const kTabPanelRect = { wxPoint(0, 0), wxSize(500, 100) };
    wxRect const kContentPanelRect = { wxPoint(0, 100), wxSize(500, 500) };
    
public:
    SettingFrame(wxWindow *parent)
    :   wxFrame(parent,
                wxID_ANY,
                "Setting",
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX)
                )
    {
        Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) {
            Destroy();
        });
        
        auto const size = kTabPanelRect.Union(kContentPanelRect).GetSize();
        SetMaxSize(size);
        SetMinSize(size);
        SetSize(size);
        
        tab_panel_ = new TabPanel(this, this);
        device_panel_ = new DeviceSettingPanel(this);
        appearance_panel_ = new AppearanceSettingPanel(this);
        plugin_panel_ = new PluginSettingPanel(this);
        active_panel_ = device_panel_;
        
        SetAutoLayout(true);
        Show(true);
    }
    
    ~SettingFrame() {
    }
    
    bool Destroy() override
    {
        return wxFrame::Destroy();
    }
    
    bool Layout() override
    {
        tab_panel_->SetSize(kTabPanelRect);

        assert(active_panel_);
        
        if(device_panel_ != active_panel_) { device_panel_->Hide(); }
        if(appearance_panel_ != active_panel_) { appearance_panel_->Hide(); }
        if(plugin_panel_ != active_panel_) { plugin_panel_->Hide(); }
        
        active_panel_->Show();
        active_panel_->SetSize(kContentPanelRect);
        
        return true;
    }
    
    void OnSelectTab(TabPanel::TabID id) override
    {
        auto id_to_panel = [this](auto id) -> wxPanel * {
            if(id == TabPanel::TabID::kDevice) { return device_panel_; }
            if(id == TabPanel::TabID::kAppearance) { return appearance_panel_; }
            if(id == TabPanel::TabID::kPlugin) { return plugin_panel_; }
            assert(false);
        };
        
        active_panel_ = id_to_panel(id);

        Layout();
        Refresh();
    }
    
private:
    TabPanel *tab_panel_ = nullptr;
    DeviceSettingPanel *device_panel_ = nullptr;
    AppearanceSettingPanel *appearance_panel_ = nullptr;
    PluginSettingPanel *plugin_panel_ = nullptr;
    wxWindow *active_panel_ = nullptr;
};

wxFrame * CreateSettingWindow(wxWindow *parent)
{
    return new SettingFrame(parent);
}

NS_HWM_END
