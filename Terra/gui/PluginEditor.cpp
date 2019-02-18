#include "PluginEditor.hpp"

#include <fmt/ostream.h>

#include "../App.hpp"
#include "./UnitData.hpp"

namespace fmt {
    template <>
    struct formatter<Steinberg::ViewRect> {
        template <typename ParseContext>
        constexpr auto parse(ParseContext &ctx) { return ctx.begin(); }
        
        template <typename FormatContext>
        auto format(Steinberg::ViewRect const &rc, FormatContext &ctx) {
            return format_to(ctx.begin(), "({}, {}, {}, {})", rc.left, rc.top, rc.right, rc.bottom);
        }
    };
}

NS_HWM_BEGIN

class ParameterSlider
:   public wxPanel
{
public:
    enum { kDefaultValueMax = 1'000'000 };
    
    UInt32 ToInteger(double normalized) const {
        return std::min<UInt32>(value_max_-1, std::round(normalized * value_max_));
    }
    
    double ToNormalized(UInt32 value) const {
        return value / (double)value_max_;
    }
    
    ParameterSlider(wxWindow *parent,
                    Vst3Plugin *plugin,
                    UInt32 param_index,
                    wxPoint pos = wxDefaultPosition,
                    wxSize size = wxDefaultSize)
    :   wxPanel(parent, wxID_ANY, pos, size)
    ,   plugin_(plugin)
    ,   param_index_(param_index)
    {
        auto param_info = plugin->GetParameterInfoByIndex(param_index);
        auto value = plugin->GetParameterValueByIndex(param_index);
        
        UInt32 const kTitleWidth = 150;
        UInt32 const kSliderWidth = size.GetWidth();
        UInt32 const kDispWidth = 100;
        
        if(param_info.step_count_ >= 1) {
            value_max_ = param_info.step_count_;
        }
        
        name_ = new wxStaticText(this, wxID_ANY, param_info.title_,
                                 wxPoint(0, 0), wxSize(kTitleWidth, size.GetHeight())
                                 );
        name_->SetToolTip(param_info.title_);
        
        slider_ = new wxSlider(this, wxID_ANY, ToInteger(value), 0, value_max_,
                               wxPoint(0, 0), wxSize(kSliderWidth, size.GetHeight()),
                               wxSL_HORIZONTAL
                               );
        
        auto init_text = plugin_->ValueToStringByIndex(param_index,
                                                       plugin_->GetParameterValueByIndex(param_index));
        disp_ = new wxTextCtrl(this, wxID_ANY, init_text, wxDefaultPosition, wxSize(kDispWidth, size.GetHeight()), wxTE_PROCESS_ENTER);
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(name_, wxSizerFlags(0).Expand());
        hbox->Add(slider_, wxSizerFlags(1).Expand());
        hbox->Add(disp_, wxSizerFlags(0).Expand());
        
        SetSizer(hbox);
        
        slider_->Bind(wxEVT_SLIDER, [id = param_info.id_, this](auto &) {
            auto const normalized = ToNormalized(slider_->GetValue());
            plugin_->EnqueueParameterChange(id, normalized);
            
            auto str = plugin_->ValueToStringByID(id, normalized);
            disp_->SetValue(str);
        });
        
        auto apply_text = [id = param_info.id_, this](auto &e) {
            hwm::dout << "EVT TEXT" << std::endl;
            auto normalized = plugin_->StringToValueByID(id, disp_->GetValue());
            if(normalized >= 0) {
                plugin_->EnqueueParameterChange(id, normalized);
                slider_->SetValue(ToInteger(normalized));
            }
        };
        
        disp_->Bind(wxEVT_TEXT_ENTER, apply_text);
        disp_->Bind(wxEVT_KILL_FOCUS, apply_text);
    }
    
    void UpdateSliderValue()
    {
        auto const normalized = plugin_->GetParameterValueByIndex(param_index_);
        slider_->SetValue(ToInteger(normalized));
        auto str = plugin_->ValueToStringByIndex(param_index_, normalized);
        disp_->SetValue(str);
    }
    
private:
    wxStaticText *name_ = nullptr;
    wxSlider *slider_ = nullptr;
    wxTextCtrl *disp_ = nullptr;
    Vst3Plugin *plugin_ = nullptr;
    UInt32 param_index_ = -1;
    UInt32 value_max_ = kDefaultValueMax;
};

class GenericParameterView
:   public wxPanel
{
    constexpr static UInt32 kParameterHeight = 20;
    constexpr static UInt32 kPageSize = 3 * kParameterHeight;
    constexpr static UInt32 kSBWidth = 20;
    
public:
    GenericParameterView(wxWindow *parent,
                         Vst3Plugin *plugin)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(40, 40))
    {
        UInt32 const num = plugin->GetNumParams();
        
        sb_ = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
        sb_->SetScrollbar(0, 1, num * kParameterHeight, 1);
        
        for(UInt32 i = 0; i < num; ++i) {
            auto slider = new ParameterSlider(this, plugin, i);
            sliders_.push_back(slider);
        }
        
        sb_->Bind(wxEVT_SCROLL_PAGEUP, [this](auto &ev) { Layout(); });
        sb_->Bind(wxEVT_SCROLL_PAGEDOWN, [this](auto &ev) { Layout(); });
        sb_->Bind(wxEVT_SCROLL_LINEUP, [this](auto &ev) { Layout(); });
        sb_->Bind(wxEVT_SCROLL_LINEDOWN, [this](auto &ev) { Layout(); });
        sb_->Bind(wxEVT_SCROLL_TOP, [this](auto &ev) { Layout(); });
        sb_->Bind(wxEVT_SCROLL_BOTTOM, [this](auto &ev) { Layout(); });
        sb_->Bind(wxEVT_SCROLL_THUMBTRACK, [this](auto &ev) { Layout(); });
        
        Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &ev) {
            hwm::dout << "{}, {}"_format(ev.GetWheelDelta(),
                                         ev.GetWheelRotation())
            << std::endl;
            sb_->SetThumbPosition(sb_->GetThumbPosition() - ev.GetWheelRotation());
            Layout();
        });
        Layout();
    }
    
    bool Layout() override
    {
        auto const rc = GetClientRect();
        
        sb_->SetSize(rc.GetWidth() - kSBWidth, 0, kSBWidth, rc.GetHeight());
        
        auto tp = sb_->GetThumbPosition();
        tp = std::min<UInt32>(tp, sliders_.size() * kParameterHeight - rc.GetHeight());
        sb_->SetScrollbar(tp, rc.GetHeight(), sliders_.size() * kParameterHeight, kPageSize);
        
        tp = sb_->GetThumbPosition();
        
        for(UInt32 i = 0; i < sliders_.size(); ++i) {
            auto s = sliders_[i];
            s->SetSize(0, i * kParameterHeight - tp, rc.GetWidth() - kSBWidth, kParameterHeight);
        }
        
        return wxPanel::Layout();
    }
    
    void UpdateParameters()
    {
        for(auto slider: sliders_) {
            slider->UpdateSliderValue();
        }
    }
    
private:
    std::vector<ParameterSlider *> sliders_;
    wxScrollBar *sb_;
};

class PluginEditorControl
:   public wxPanel
{
public:
    class Listener : public IListenerBase
    {
    protected:
        Listener() {}
    public:
        virtual void OnChangeEditorType(bool use_generic_editor) {}
        virtual void OnChangeProgram() {}
    };
    
    PluginEditorControl(wxWindow *parent,
                        Vst3Plugin *plugin,
                        wxPoint pos = wxDefaultPosition,
                        wxSize size = wxDefaultSize)
    :   wxPanel(parent, wxID_ANY, pos, size)
    ,   plugin_(plugin)
    {
        cho_select_unit_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(100, size.GetHeight()));
        cho_select_program_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(100, size.GetHeight()));
        btn_prev_program_ = new wxButton(this, wxID_ANY, L"<<", wxDefaultPosition, wxSize(40, size.GetHeight()));
        btn_next_program_ = new wxButton(this, wxID_ANY, L">>", wxDefaultPosition, wxSize(40, size.GetHeight()));
        chk_gen_editor_ = new wxCheckBox(this, wxID_ANY, L"Generic Editor", wxDefaultPosition, wxSize(120, size.GetHeight()));
        
        auto list = GetSelectableUnitInfos(plugin);
        for(auto const &info: list) {
            cho_select_unit_->Append(info.name_, new UnitData{info.id_});
        }
        
        if(cho_select_unit_->GetCount() == 0) {
            cho_select_unit_->Disable();
            cho_select_program_->Disable();
            btn_prev_program_->Disable();
            btn_next_program_->Disable();
        } else {
            cho_select_unit_->SetSelection(0);
            for(auto &prog: list[0].program_list_.programs_) {
                cho_select_program_->Append(prog.name_);
            }
            cho_select_program_->SetSelection(plugin->GetProgramIndex(0));
        }
        
        plugin->CheckHavingEditor();
        if(plugin->HasEditor()) {
            chk_gen_editor_->SetValue(false);
        } else {
            chk_gen_editor_->SetValue(true);
            chk_gen_editor_->Disable();
        }
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(cho_select_unit_, wxSizerFlags(0).Expand());
        hbox->Add(cho_select_program_, wxSizerFlags(0).Expand());
        hbox->Add(btn_prev_program_, wxSizerFlags(0).Expand());
        hbox->Add(btn_next_program_, wxSizerFlags(0).Expand());
        hbox->Add(chk_gen_editor_, wxSizerFlags(0).Expand());
        hbox->AddStretchSpacer(1);
        
        SetSizer(hbox);
        
        cho_select_unit_->Bind(wxEVT_CHOICE, [this](auto &ev) {
            current_unit_ = cho_select_unit_->GetSelection();
            UpdateProgramList();
        });
        
        cho_select_program_->Bind(wxEVT_CHOICE, [this](auto &ev) {
            plugin_->SetProgramIndex(cho_select_program_->GetSelection(), GetUnitID(cho_select_unit_));
            listeners_.Invoke([](auto *x) {
                x->OnChangeProgram();
            });
        });
        
        btn_prev_program_->Bind(wxEVT_BUTTON, [this](auto &ev) {
            int const current_program = cho_select_program_->GetSelection();
            if(current_program != 0) {
                cho_select_program_->SetSelection(current_program-1);
                plugin_->SetProgramIndex(current_program-1, GetUnitID(cho_select_unit_));
                listeners_.Invoke([](auto *x) {
                    x->OnChangeProgram();
                });
            }
        });

        btn_next_program_->Bind(wxEVT_BUTTON, [this](auto &ev) {
            int const current_program = cho_select_program_->GetSelection();
            if(current_program != cho_select_program_->GetCount()-1) {
                cho_select_program_->SetSelection(current_program+1);
                plugin_->SetProgramIndex(current_program+1, GetUnitID(cho_select_unit_));
                listeners_.Invoke([](auto *x) {
                    x->OnChangeProgram();
                });
            }
        });
        chk_gen_editor_->Bind(wxEVT_CHECKBOX, [this](auto &ev) {
            listeners_.Invoke([this](auto *x) {
                x->OnChangeEditorType(chk_gen_editor_->IsChecked());
            });
        });
    }
    
    ~PluginEditorControl()
    {}
    
    using IListenerService = IListenerService<Listener>;
    
    IListenerService & GetListeners() { return listeners_; }
    
private:
    Vst3Plugin *plugin_ = nullptr;
    wxChoice *cho_select_unit_;
    wxChoice *cho_select_program_;
    wxButton *btn_next_program_;
    wxButton *btn_prev_program_;
    wxCheckBox *chk_gen_editor_;
    ListenerService<Listener> listeners_;
    
    struct UnitInfo {
        int unit_index_;
        int program_index_;
    };
    
    std::vector<int> programs_;
    int current_unit_ = -1;
    
    Vst3Plugin::UnitID GetCurrentUnitID() const
    {
        auto const sel = cho_select_unit_->GetSelection();
        if(sel == wxNOT_FOUND) { return -1; }
        
        assert(cho_select_unit_->HasClientObjectData());
        auto data = static_cast<UnitData *>(cho_select_unit_->GetClientObject(sel));
        return data->unit_id_;
    }
    
    void UpdateProgramList() {
        assert(cho_select_unit_->GetSelection() != wxNOT_FOUND);
        
        auto unit_id = GetUnitID(cho_select_unit_);
        
        cho_select_program_->Clear();
        
        auto unit_info = plugin_->GetUnitInfoByID(unit_id);
        for(auto &prog: unit_info.program_list_.programs_) {
            cho_select_program_->Append(prog.name_);
        }
        
        cho_select_program_->SetSelection(plugin_->GetProgramIndex(unit_id));
    }
};

class PluginEditorContents
:   public wxWindow
,   public Vst3Plugin::PlugFrameListener
{
public:
    PluginEditorContents(wxWindow *parent,
                         Vst3Plugin *target_plugin)
    :   wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    {
        plugin_ = target_plugin;
        
        // いくつかのプラグイン (Arturia SEM V2やOszillos Mega Scopeなど) では、
        // IPlugViewに実際のウィンドウハンドルをattachするまえに、IPlugViewのサイズでウィンドウのサイズを設定しておかなければ、
        // 正しくプラグインウィンドウが表示されなかった。
        auto rc = plugin_->GetPreferredRect();
        OnResizePlugView(rc);
        
        plugin_->OpenEditor(GetHandle(), this);
        
        Bind(wxEVT_SHOW, [this](auto &ev) {
            if(IsShown() && plugin_->IsEditorOpened() == false) {
                auto rc = plugin_->GetPreferredRect();
                OnResizePlugView(rc);
                plugin_->OpenEditor(GetHandle(), this);
            } else {
                plugin_->CloseEditor();
            }
        });
        
        Show(true);
    }
    
    ~PluginEditorContents() {
        int x = 0;
        x = 1;
    }
    
    void CloseEditor()
    {
        plugin_->CloseEditor();
    }
    
private:
    Vst3Plugin *plugin_ = nullptr;
    
    void OnResizePlugView(Steinberg::ViewRect const &rc) override
    {
        hwm::dout << "New view size: {}"_format(rc) << std::endl;
        
        wxSize size(rc.getWidth(), rc.getHeight());
        SetSize(size);
        SetMinSize(size);
        SetMaxSize(size);
        GetParent()->Layout();
    }
};

class PluginEditorFrame
:   public wxFrame
,   public PluginEditorControl::Listener
{
    static constexpr UInt32 kControlHeight = 20;
public:
    PluginEditorFrame(wxWindow *parent,
                      Vst3Plugin *target_plugin,
                      std::function<void()> on_destroy)
    :   wxFrame(parent,
                wxID_ANY,
                target_plugin->GetEffectName(),
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX)
                )
    {
        on_destroy_ = on_destroy;
        
        control_ = new PluginEditorControl(this, target_plugin);
        control_->GetListeners().AddListener(this);
        
        genedit_ = new GenericParameterView(this, target_plugin);
        
        if(target_plugin->HasEditor()) {
            contents_ = new PluginEditorContents(this, target_plugin);
            genedit_->Hide();
        } else {
            genedit_->Show();
        }
        
        SetMaxSize(wxSize(1000, 1000));
        SetMinSize(wxSize(10, 10));
        
        SetAutoLayout(true);
        Show(true);
    }
    
    ~PluginEditorFrame() {
        control_->GetListeners().RemoveListener(this);
    }
    
    bool Destroy() override
    {
        if(contents_) {
            contents_->CloseEditor();
        }
        
        on_destroy_();
        return wxFrame::Destroy();
    }
    
    bool Layout() override
    {
        if(contents_ && contents_->IsShown()) {
            auto rc = contents_->GetSize();
            rc.IncBy(0, kControlHeight);
            SetClientSize(rc);
            
            control_->SetSize(0, 0, rc.GetWidth(), kControlHeight);
            contents_->SetSize(0, kControlHeight, rc.GetWidth(), rc.GetHeight() - kControlHeight);
        } else if(genedit_ && genedit_->IsShown()) {
            auto rc = GetClientSize();
            
            control_->SetSize(0, 0, rc.GetWidth(), kControlHeight);
            genedit_->SetSize(0, kControlHeight, rc.GetWidth(), rc.GetHeight() - kControlHeight);
            genedit_->Layout();
        }
        return true;
    }
    
private:
    void OnChangeEditorType(bool use_generic_editor) override
    {
        if(use_generic_editor) {
            genedit_->UpdateParameters();
            genedit_->Show();
            contents_->Hide();
            contents_->SetSize(1, 1);
            SetSize(500, 500);
            SetMinSize(wxSize(10, 10));
            SetMaxSize(wxSize(2000, 2000));
            auto style = (GetWindowStyle() | wxRESIZE_BORDER);
            SetWindowStyle(style);
        } else {
            genedit_->Hide();
            contents_->Show();
            auto style = (GetWindowStyle() & (~wxRESIZE_BORDER));
            SetWindowStyle(style);
        }
        Layout();
    }
    
    void OnChangeProgram() override
    {
        if(genedit_->IsShown()) {
            genedit_->UpdateParameters();
        }
    }
    
private:
    std::function<void()> on_destroy_;
    Vst3Plugin *plugin_ = nullptr;
    PluginEditorContents *contents_ = nullptr;
    PluginEditorControl *control_ = nullptr;
    GenericParameterView *genedit_ = nullptr;
};

wxFrame * CreatePluginEditorFrame(wxWindow *parent,
                                  Vst3Plugin *target_plugin,
                                  std::function<void()> on_destroy)
{
    return new PluginEditorFrame(parent, target_plugin, on_destroy);
}

NS_HWM_END
