#include "SettingDialog.hpp"
#include "Util.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "../project/Project.hpp"

NS_HWM_BEGIN

class DeviceSettingPanel
:   public wxPanel
{
    BrushPen background = HSVToColour(0.4, 0.7, 0.9);
    wxPen title = HSVToColour(0.0, 0.0, 1.0);
    
    struct AudioDeviceInfoWrapper : wxClientData
    {
        AudioDeviceInfoWrapper(AudioDeviceInfo const &info)
        :   info_(info)
        {}
        
        AudioDeviceInfo info_;
    };
    
    struct DeviceSetting
    {
        DeviceSetting() {}
        DeviceSetting(AudioDevice *dev)
        {
            auto get_info = [](auto dev, auto io_type) -> std::optional<AudioDeviceInfo> {
                auto p = dev->GetDeviceInfo(io_type);
                if(p) { return *p; }
                else { return std::nullopt; }
            };
            
            input_info_ = get_info(dev, DeviceIOType::kInput);
            output_info_ = get_info(dev, DeviceIOType::kOutput);
            sample_rate_ = dev->GetSampleRate();
            block_size_ = dev->GetBlockSize();
        }
        
        std::vector<double> GetAvailableSamplingRates() const
        {
            if(input_info_ && output_info_) {
                std::vector<double> const &xs = output_info_->supported_sample_rates_;
                std::vector<double> dest;
                std::copy_if(xs.begin(), xs.end(), std::back_inserter(dest), [this](auto x) {
                    return input_info_->IsSampleRateSupported(x);
                });
                return dest;
            } else if(input_info_) {
                return input_info_->supported_sample_rates_;
            } else if(output_info_) {
                return output_info_->supported_sample_rates_;
            } else {
                return {};
            }
        }
        
        std::optional<AudioDeviceInfo> input_info_;
        std::optional<AudioDeviceInfo> output_info_;
        double sample_rate_ = 0;
        SampleCount block_size_ = 0;
    };
    
    //! 最新のデバイス状態を表す
    DeviceSetting device_setting_;
 
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
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
    
        auto add_entry = [&](auto parent_box, auto static_text, auto choice) {
            //int const kMinHeight = 20;
            auto hbox = new wxBoxSizer(wxHORIZONTAL);
            //hbox->SetMinSize(1, kMinHeight);
            static_text->SetMinSize(wxSize(150, 1));
            hbox->Add(static_text, wxSizerFlags(0).Expand());
            hbox->Add(choice, wxSizerFlags(1).Expand());
            parent_box->Add(hbox, wxSizerFlags(0).Expand().Border(wxTOP|wxBOTTOM, 5));
        };
        
        add_entry(vbox, st_audio_inputs_, cho_audio_inputs_);
        add_entry(vbox, st_audio_outputs_, cho_audio_outputs_);
        add_entry(vbox, st_sample_rates_, cho_sample_rates_);
        add_entry(vbox, st_buffer_sizes_, cho_buffer_sizes_);
                
        auto outer_box = new wxBoxSizer(wxHORIZONTAL);
        outer_box->Add(vbox, wxSizerFlags(1).Expand().Border(wxALL,  5));
        SetSizer(outer_box);
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        
        cho_audio_inputs_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectAudioInput(); });
        cho_audio_outputs_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectAudioOutput(); });
        cho_sample_rates_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectSampleRate(); });
        cho_buffer_sizes_->Bind(wxEVT_CHOICE, [this](auto &ev) { OnSelectBufferSize(); });
        
        InitializeList();
    }
    
    //! 2つのAudioDeviceInfoが指すデバイスが同じものかどうかを返す。
    //! サポートしているサンプリングレートの違いは関知しない。
    static
    bool is_same_device(AudioDeviceInfo const &x, AudioDeviceInfo const &y)
    {
        auto to_tuple = [](auto info) {
            return std::tie(info.driver_, info.io_type_, info.name_, info.num_channels_);
        };
        
        return to_tuple(x) == to_tuple(y);
    }
    
    template<class T>
    static
    T * to_ptr(std::optional<T> &opt) { return (opt ? &*opt : nullptr); }
    
    template<class T>
    static
    T const * to_ptr(std::optional<T> const &opt) { return (opt ? &*opt : nullptr); }
    
    template<class T>
    static
    std::optional<T> to_optional(T *p) { return (p ? *p : std::optional<T>{}); }
    
    void OnSelectAudioInput()
    {
        auto *cho = cho_audio_inputs_;
        
        auto sel = cho->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto copied = device_setting_;
        auto wrapper = dynamic_cast<AudioDeviceInfoWrapper *>(cho->GetClientObject(sel));
        
        if(is_same_device(copied.input_info_.value_or(AudioDeviceInfo{}),
                          wrapper ? wrapper->info_ : AudioDeviceInfo{}))
        {
            return;
        }
        
        copied.input_info_ = wrapper ? wrapper->info_ : std::optional<AudioDeviceInfo>{};
        OpenDevice(copied, true);
    }
    
    void OnSelectAudioOutput()
    {
        auto *cho = cho_audio_outputs_;
        
        auto sel = cho->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto copied = device_setting_;
        auto wrapper = dynamic_cast<AudioDeviceInfoWrapper *>(cho->GetClientObject(sel));
        assert(wrapper);
        
        if(is_same_device(copied.output_info_.value_or(AudioDeviceInfo{}),
                          wrapper->info_))
        {
            return;
        }
        
        copied.output_info_ = wrapper->info_;
        OpenDevice(copied, false);
    }
    
    void OnSelectSampleRate()
    {
        auto *cho = cho_sample_rates_;
        
        auto sel = cho->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto copied = device_setting_;
        double new_rate = 0;
        if(cho->GetStringSelection().ToDouble(&new_rate) == false) { return; }
        
        if(copied.sample_rate_ == new_rate) { return; }
        
        copied.sample_rate_ = new_rate;
        OpenDevice(copied, false);
    }
    
    void OnSelectBufferSize()
    {
        auto *cho = cho_buffer_sizes_;
        
        auto sel = cho->GetSelection();
        if(sel == wxNOT_FOUND) { return; }
        
        auto copied = device_setting_;
        long new_block_size = 0;
        if(cho->GetStringSelection().ToLong(&new_block_size) == false) { return; }
        
        if(copied.block_size_ == new_block_size) { return; }
        
        copied.block_size_ = new_block_size;
        OpenDevice(copied, false);
    }
    
    std::optional<AudioDeviceInfo> GetOppositeDeviceInfo(AudioDeviceInfo const &info) const
    {
        wxChoice *op_cho;
        int index_start = 0;
        
        if(info.io_type_ == DeviceIOType::kInput) {
            op_cho = cho_audio_outputs_;
            index_start = 0;
        } else {
            op_cho = cho_audio_outputs_;
            index_start = 1;
        }
        
        // 同じDriverのものだけ集める
        std::vector<AudioDeviceInfo const *> xs;
        for(int i = index_start; i < op_cho->GetCount(); ++i) {
            auto p = dynamic_cast<AudioDeviceInfoWrapper *>(op_cho->GetClientObject(i));
            assert(p);
            
            if(p->info_.driver_ == info.driver_) {
                xs.push_back(&p->info_);
            }
        }
        if(xs.empty()) { return std::nullopt; }
        
        auto found = std::find_if(xs.begin(), xs.end(), [&info](auto x) {
            return x->name_ == info.name_;
        });
        
        if(found != xs.end()) { return **found; }
        return *xs[0];
    }
    
    void OpenDevice(DeviceSetting new_setting, bool is_input_changed)
    {
        auto adm = AudioDeviceManager::GetInstance();
        adm->Close();
        
        auto open_impl = [](DeviceSetting &ds) -> AudioDeviceManager::OpenResult {
            auto in = to_ptr(ds.input_info_);
            auto out = to_ptr(ds.output_info_);
            assert(out);
            
            auto rate = ds.sample_rate_;
            auto block = ds.block_size_;
            
            auto available_sample_rates = ds.GetAvailableSamplingRates();
            if(available_sample_rates.empty()) {
                //! 適用可能なサンプリングレートがない
                return AudioDeviceManager::Error(AudioDeviceManager::kInvalidParameters,
                                                 L"No valid sampling rates.");
            }
            
            if(std::none_of(available_sample_rates.begin(),
                            available_sample_rates.end(),
                            [rate](auto x) { return x == rate; }))
            {
                rate = available_sample_rates[0];
            }
            
            auto adm = AudioDeviceManager::GetInstance();
            auto result = adm->Open(in, out, rate, block);
            if(result.is_right()) {
                ds.sample_rate_ = rate;
            }
            
            return result;
        };
        
        auto result = open_impl(new_setting);
        
        if(result.is_right() == false) {
            if(is_input_changed && new_setting.input_info_) {
                auto info = GetOppositeDeviceInfo(*new_setting.input_info_);
                if(info) {
                    new_setting.output_info_ = info;
                    result = open_impl(new_setting);
                }
            } else {
                auto info = GetOppositeDeviceInfo(*new_setting.output_info_);
                new_setting.output_info_ = info;
                result = open_impl(new_setting);
            }
        }
    
        if(result.is_right() == false) {
            auto p = dynamic_cast<AudioDeviceInfoWrapper *>(cho_audio_outputs_->GetClientObject(0));
            assert(p);
            new_setting.input_info_ = std::nullopt;
            new_setting.output_info_ = p->info_;
            result = open_impl(new_setting);
        }
        
        if(result.is_right()) {
            device_setting_ = new_setting;
            UpdateSelections();
        } else {
            wxMessageBox(L"Opening audio device failed: " + result.left().error_msg_);
        }
    }
    
    //! デバイス名とドライバ名のセット
    static
    String get_device_label(AudioDeviceInfo const &info)
    {
        return info.name_ + L" (" + to_string(info.driver_) + L")";
    }
    
    static
    String sample_rate_to_string(double rate)
    {
        std::wstringstream ss;
        ss << rate;
        return ss.str();
    }
    
    void UpdateSelections()
    {
        auto select = [](wxChoice *cho, String const &label) {
            for(int i = 0; i < cho->GetCount(); ++i) {
                if(cho->GetString(i) == label) {
                    cho->SetSelection(i);
                    break;
                }
            }
        };
        
        if(device_setting_.input_info_) {
            select(cho_audio_inputs_, get_device_label(*device_setting_.input_info_));
        } else {
            cho_audio_inputs_->SetSelection(0);
        }

        assert(device_setting_.output_info_);
        select(cho_audio_outputs_, get_device_label(*device_setting_.output_info_));

        cho_sample_rates_->Clear();
        auto available = device_setting_.GetAvailableSamplingRates();
        for(auto rate: available) { cho_sample_rates_->Append(sample_rate_to_string(rate)); }
        select(cho_sample_rates_, sample_rate_to_string(device_setting_.sample_rate_));

        cho_buffer_sizes_->Clear();
        for(auto block: { 16, 32, 64, 128, 256, 378, 512, 768, 1024, 2048, 4096 }) {
            cho_buffer_sizes_->Append(std::to_string(block));
        }
        select(cho_buffer_sizes_, std::to_wstring(device_setting_.block_size_));
    }
    
    void InitializeList()
    {
        auto adm = AudioDeviceManager::GetInstance();
        if(adm->IsOpened()) {
            device_setting_ = DeviceSetting(adm->GetDevice());
        }
        adm->Close();
        
        auto list = adm->Enumerate();
        
        cho_audio_inputs_->Clear();
        cho_audio_outputs_->Clear();
        cho_sample_rates_->Clear();
        cho_buffer_sizes_->Clear();
        
        cho_audio_inputs_->Append("Disable");
        
        for(auto &entry: list) {
            if(entry.io_type_ == DeviceIOType::kInput) {
                cho_audio_inputs_->Append(get_device_label(entry), new AudioDeviceInfoWrapper{entry});
            } else {
                cho_audio_outputs_->Append(get_device_label(entry), new AudioDeviceInfoWrapper{entry});
            }
        }
        
        OpenDevice(device_setting_, false);
    }
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        
        background.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
    
private:
    wxStaticText *st_audio_inputs_ = nullptr;
    wxChoice *cho_audio_inputs_ = nullptr;
    wxStaticText *st_audio_outputs_ = nullptr;
    wxChoice *cho_audio_outputs_ = nullptr;
    wxStaticText *st_sample_rates_ = nullptr;
    wxChoice *cho_sample_rates_ = nullptr;
    wxStaticText *st_buffer_sizes_ = nullptr;
    wxChoice *cho_buffer_sizes_ = nullptr;
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
:   public wxDialog
,   TabPanel::Callback
{
    wxRect const kTabPanelRect = { wxPoint(0, 0), wxSize(500, 100) };
    wxRect const kContentPanelRect = { wxPoint(0, 100), wxSize(500, 500) };
    
public:
    SettingFrame(wxWindow *parent)
    :   wxDialog(parent, wxID_ANY, "Setting")
    {        
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
        
        auto pj = Project::GetCurrentProject();
        pj->Deactivate();
        Bind(wxEVT_CLOSE_WINDOW, [pj, this](auto &ev) {
            pj->Activate();
            EndModal(wxID_OK);
        });
        
        Show(true);
    }
    
    ~SettingFrame() {
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

wxDialog * CreateSettingDialog(wxWindow *parent)
{
    return new SettingFrame(parent);
}

NS_HWM_END
