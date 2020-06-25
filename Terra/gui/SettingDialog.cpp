#include "SettingDialog.hpp"

#include <unordered_map>
#include <wx/dirdlg.h>

#include "Util.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "../project/Project.hpp"
#include "../resource/ResourceHelper.hpp"
#include "../App.hpp"

NS_HWM_BEGIN

BrushPen const kPanelBackgroundColour = HSVToColour(0, 0, 14 / 100.0);

class DeviceSettingPanel
:   public wxPanel
{
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
        
        st_audio_inputs_->SetForegroundColour(HSVToColour(0.0, 0.0, 0.9));
        st_audio_inputs_->SetBackgroundColour(kPanelBackgroundColour.brush_.GetColour());

        st_audio_outputs_->SetForegroundColour(HSVToColour(0.0, 0.0, 0.9));
        st_audio_outputs_->SetBackgroundColour(kPanelBackgroundColour.brush_.GetColour());

        st_sample_rates_->SetForegroundColour(HSVToColour(0.0, 0.0, 0.9));
        st_sample_rates_->SetBackgroundColour(kPanelBackgroundColour.brush_.GetColour());

        st_buffer_sizes_->SetForegroundColour(HSVToColour(0.0, 0.0, 0.9));
        st_buffer_sizes_->SetBackgroundColour(kPanelBackgroundColour.brush_.GetColour());
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
    
        auto add_entry = [&](auto parent_box, auto static_text, auto choice) {
            auto hbox = new wxBoxSizer(wxHORIZONTAL);
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
        
        SetAutoLayout(true);
        SetCanFocus(false);
        InitializeList();
        Layout();
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
        return info.name_ + L" (" + to_wstring(info.driver_) + L")";
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
        wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        
        kPanelBackgroundColour.ApplyTo(dc);
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

class GeneralSettingPanel
:   public wxPanel
{
public:
    GeneralSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
    }
    
    void OnPaint()
    {
		wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        
        kPanelBackgroundColour.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
};

class AppearanceSettingPanel
:   public wxPanel
{
public:
    AppearanceSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
    }
    
    void OnPaint()
    {
        wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        
        kPanelBackgroundColour.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
};

class PluginSettingPanel
:   public wxPanel
{
public:
    PluginSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        
        st_dir_list_ = new wxStaticText(this, wxID_ANY, "VST3 Plugin Directories");
        lb_dir_list_ = new wxListBox(this, wxID_ANY);
        btn_plus_ = new wxButton(this, wxID_ANY, "+");
        btn_minus_ = new wxButton(this, wxID_ANY, "-");
        btn_list_operation_ = new wxButton(this, wxID_ANY, "▼");
        
        st_dir_list_->SetForegroundColour(HSVToColour(0.0, 0.0, 0.9));
        st_dir_list_->SetBackgroundColour(kPanelBackgroundColour.brush_.GetColour());
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
        st_dir_list_->SetMaxSize({1000, 50});
        lb_dir_list_->SetMaxSize({1000, 300});
        vbox->Add(st_dir_list_, wxSizerFlags(0).Expand().Border());
        vbox->Add(lb_dir_list_, wxSizerFlags(1).Expand().Border());
        
        {
            auto hbox = new wxBoxSizer(wxHORIZONTAL);
            hbox->AddStretchSpacer(1);
            btn_plus_->SetMaxSize({50, 50});
            btn_minus_->SetMaxSize({50, 50});
            hbox->Add(btn_plus_, wxSizerFlags(1).Expand());
            hbox->Add(btn_minus_, wxSizerFlags(1).Expand());
            vbox->Add(hbox, wxSizerFlags(0).Expand().Border());
        }
        
        {
            auto hbox = new wxBoxSizer(wxHORIZONTAL);
            hbox->AddStretchSpacer(1);
            btn_list_operation_->SetMaxSize({100, 50});
            hbox->Add(btn_list_operation_, wxSizerFlags(1).Expand());
            vbox->Add(hbox, wxSizerFlags(0).Expand().Border());
        }
        
        btn_plus_->Bind(wxEVT_BUTTON, [this](auto &) { OnAddDirectory(); });
        btn_minus_->Bind(wxEVT_BUTTON, [this](auto &) { OnRemoveDirectory(); });
        btn_list_operation_->Bind(wxEVT_BUTTON, [this](auto &) { OnShowListOperationMenu(); });
        lb_dir_list_->Bind(wxEVT_LISTBOX, [this](auto &) { OnListboxChanged(); });
        
        OnRestoreList();
        OnListboxChanged();
        
        SetSizer(vbox);
    }
    
    void OnPaint()
    {
		wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        
        kPanelBackgroundColour.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
    
    void OnAddDirectory()
    {
        wxDirDialog dlg(this, "Select VST3 Directory", "",
                        wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        
        if(dlg.ShowModal() == wxID_CANCEL) {
            return;
        }
        
        auto const dir = dlg.GetPath();
        
        if(wxDirExists(dir) == false) {
            return;
        }
        
        for(int i = 0, end = lb_dir_list_->GetCount(); i < end; ++i) {
            auto const entry = lb_dir_list_->GetString(i);
            if(wxFileName(dir, "").SameAs(wxFileName(entry, ""))) {
                return;
            }
        }
        
        lb_dir_list_->Append(dir);
        OnListboxChanged();
    }
    
    void OnRemoveDirectory()
    {
        auto selected = lb_dir_list_->GetSelection();
        if(selected == wxNOT_FOUND) { return; }
        
        lb_dir_list_->Delete(selected);
        OnListboxChanged();
    }
    
    void OnShowListOperationMenu()
    {
        wxMenu m;
         
        static constexpr int kRescan = 1000;
        static constexpr int kRescanAll = 1001;
        static constexpr int kRestoreList = 1002;
        
        wxMenu menu;
        auto item_rescan        = menu.Append(kRescan, "Apply and Rescan", "Apply list changes and rescan plugins.");
        auto item_rescan_all    = menu.Append(kRescanAll, "Apply and Rescan All", "Apply list changes and rescan plugins. All pre-scanned plugins will be rescanned again.");
        auto item_restore       = menu.Append(kRestoreList, "Restore list changes", "Restore list changes");
        
        auto const dir_list_changed = GetCurrentDirectoryList() != GetAppliedDirectoryList();
        if(dir_list_changed == false) {
            item_rescan->Enable(false);
            item_restore->Enable(false);
        }
                    
        menu.Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) {
            switch(ev.GetId()) {
                case kRescan: { OnApplyAndRescanPlugins(RescanMode::kNormal); break; }
                case kRescanAll: { OnApplyAndRescanPlugins(RescanMode::kForce); break; }
                case kRestoreList: { OnRestoreList(); break; }
            }
        });
        
        PopupMenu(&menu, btn_list_operation_->GetPosition());
    }
    
    std::vector<String> GetCurrentDirectoryList() const
    {
        std::vector<String> tmp;
        for(int i = 0, end = lb_dir_list_->GetCount(); i < end; ++i) {
            tmp.push_back(lb_dir_list_->GetString(i).ToStdWstring());
        }
        
        return tmp;
    }
    
    std::vector<String> GetAppliedDirectoryList() const
    {
        auto app = App::GetInstance();
        return app->GetVst3PluginSearchPaths();
    }
    
    enum class RescanMode {
        kNormal,
        kForce,
    };
    
    void OnApplyAndRescanPlugins(RescanMode mode)
    {
        auto app = App::GetInstance();
        auto list = GetCurrentDirectoryList();
        assert(list.empty() == false);
        
        app->SetVst3PluginSearchPaths(list);
        
        if(mode == RescanMode::kNormal) {
            app->RescanPlugins();
        } else if(mode == RescanMode::kForce) {
            app->ForceRescanPlugins();
        }
    }
    
    void OnRestoreList()
    {
        lb_dir_list_->Clear();
        for(auto const &entry: GetAppliedDirectoryList()) {
            lb_dir_list_->Append(entry);
        }
        
        lb_dir_list_->Select(wxNOT_FOUND);
    }
    
    void OnListboxChanged()
    {
        auto const selected_any = lb_dir_list_->GetSelection() != wxNOT_FOUND;
        btn_minus_->Enable(selected_any);

        if(lb_dir_list_->IsEmpty()) {
            OnRestoreList();
        }
    }
    
private:
    wxStaticText *st_dir_list_ = nullptr;
    wxListBox *lb_dir_list_ = nullptr;
    wxButton *btn_plus_ = nullptr;
    wxButton *btn_minus_ = nullptr;
    wxButton *btn_list_operation_ = nullptr;
};

class LocationSettingPanel
:   public wxPanel
{
public:
    LocationSettingPanel(wxWindow *parent)
    :   wxPanel(parent)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
    }
    
    void OnPaint()
    {
		wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        
        kPanelBackgroundColour.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
    }
};

static wxSize kSettingIconSize = { 64, 64 };

class TabPanel
:   public wxPanel
{
public:
    enum class TabID {
        kDevice,
        kGeneral,
        kAppearance,
        kLocation,
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
        auto add_icon = [this](auto id, String filename) {
            auto image = GetResourceAs<wxImage>({L"setting", filename});
            if(image.IsOk() == false) {
                // assign "not found" image
            }
            icons_[id] = wxBitmap(image);
        };
        
        add_icon(TabID::kDevice, L"Speaker.png");
        add_icon(TabID::kGeneral, L"Gear.png");
        add_icon(TabID::kAppearance, L"Brush.png");
        add_icon(TabID::kPlugin, L"Plugin.png");
        add_icon(TabID::kLocation, L"Folder.png");
        highlight_ = GetResourceAs<wxImage>(L"setting/Button Highlight.png");
        
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        Bind(wxEVT_SIZE, [this](auto &ev) { OnSize(); });
    }
    
    void OnSize()
    {
        auto size = GetClientSize();
        auto image = GetResourceAs<wxImage>(L"setting/Background.png");
        image = image.Scale(size.GetWidth(), size.GetHeight());
        
        background_ = wxBitmap(image);
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
        
        wxPoint const pos((int)id * kSettingIconSize.GetWidth(), 0);
        return wxRect(pos, kSettingIconSize);
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
		wxPaintDC pdc(this);
		wxGCDC dc(pdc);
        
        dc.DrawBitmap(background_, 0, 0);
        
        for(int i = 0; i < (int)TabID::kNumIDs; ++i) {
            auto &icon = icons_[(TabID)i];
            wxPoint pos { i * kSettingIconSize.GetWidth(), 0 };
            dc.DrawBitmap(icon, pos);
            if(i == (int)current_tab_) {
                dc.DrawBitmap(highlight_, pos);
            }
        }
    }
    
    TabID GetCurrentTab() const { return current_tab_; }
    
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
    std::unordered_map<TabID, wxBitmap> icons_;
    wxBitmap highlight_;
    wxBitmap background_;
};

class SettingFrame
:   public wxDialog
,   TabPanel::Callback
{
    wxRect const kRect = { wxPoint(0, 0), wxSize(500, 600) };
    
    wxRect const kTabPanelRect = {
        wxPoint(0, 0),
        wxSize(kRect.GetWidth(), kSettingIconSize.GetHeight())
    };
    
    wxRect const kContentPanelRect = {
        wxPoint(0, kSettingIconSize.GetHeight()),
        wxSize(kRect.GetWidth(), kRect.GetHeight() - kSettingIconSize.GetHeight())
    };
    
public:
    SettingFrame(wxWindow *parent)
    :   wxDialog(parent, wxID_ANY, "Setting")
    {
        tab_panel_ = new TabPanel(this, this);
        
        SetDoubleBuffered(true);
        
        for(int i = 0; i < (int)TabPanel::TabID::kNumIDs; ++i) {
            auto const id = (TabPanel::TabID)i;
            panels_[id] = CreatePanel(id);
            assert(panels_[id] != nullptr);
        }
        
        active_panel_ = panels_[TabPanel::TabID::kDevice];
        
        auto const size = kTabPanelRect.Union(kContentPanelRect).GetSize();
        SetMaxSize(size);
        SetMinSize(size);
        SetClientSize(size);
        
        SetAutoLayout(true);
        
        auto pj = Project::GetCurrentProject();
        pj->Deactivate();
        Bind(wxEVT_CLOSE_WINDOW, [pj, this](auto &ev) {
            pj->Activate();
            EndModal(wxID_OK);
        });
        Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &ev) {
            OnCharHook(ev);
        });

        Layout();
        Show(true);
    }
    
    ~SettingFrame() {
    }
    
    void MoveToNextTab()
    {
        tab_panel_->MoveToNextTab();
        OnSelectTab(tab_panel_->GetCurrentTab());
        Layout();
    }
    
    void MoveToPrevTab()
    {
        tab_panel_->MoveToPrevTab();
        OnSelectTab(tab_panel_->GetCurrentTab());
        Layout();
    }
    
    void OnCharHook(wxKeyEvent &ev)
    {
        if(ev.GetUnicodeKey() == L'\t') {
            MoveToNextTab();
            return;
        }
        
        if(ev.GetUnicodeKey() == L'\x19') {
            MoveToPrevTab();
            return;
        }
        
        if(ev.GetUnicodeKey() == L'\x1b') {
            this->Close();
        }

        if(ev.GetUnicodeKey() != WXK_NONE) {
            ev.DoAllowNextEvent();
            return;
        } else {
            auto const key_code = ev.GetKeyCode();
        
            if(key_code == WXK_LEFT) {
                MoveToPrevTab();
            } else if(key_code == WXK_RIGHT) {
                MoveToNextTab();
            } else {
                ev.DoAllowNextEvent();
                return;
            }
        }
    }
    
    bool Layout() override
    {
        tab_panel_->SetSize(kTabPanelRect);

        assert(active_panel_);
        
        for(auto entry: panels_) {
            auto panel = entry.second;
            if(panel != active_panel_) {
                panel->Hide();
            }
        }
        
        active_panel_->Show();
        active_panel_->SetSize(kContentPanelRect);
        
        return true;
    }
    
    wxPanel * CreatePanel(TabPanel::TabID id)
    {
        if(id == TabPanel::TabID::kDevice) { return new DeviceSettingPanel(this); }
        if(id == TabPanel::TabID::kAppearance) { return new AppearanceSettingPanel(this); }
        if(id == TabPanel::TabID::kPlugin) { return new PluginSettingPanel(this); }
        if(id == TabPanel::TabID::kLocation) { return new LocationSettingPanel(this); }
        if(id == TabPanel::TabID::kGeneral) { return new GeneralSettingPanel(this); }
        assert(false);
        return nullptr;
    };
    
    void OnSelectTab(TabPanel::TabID id) override
    {
        active_panel_ = panels_[id];
        Layout();
        Refresh();
    }
    
private:
    TabPanel *tab_panel_ = nullptr;
    std::unordered_map<TabPanel::TabID, wxPanel *> panels_;
    wxWindow *active_panel_ = nullptr;
};

wxDialog * CreateSettingDialog(wxWindow *parent)
{
    return new SettingFrame(parent);
}

NS_HWM_END
