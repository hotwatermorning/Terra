#include "PluginEditor.hpp"

#include <fmt/ostream.h>

#include "../App.hpp"

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
    class Listener
    {
    protected:
        Listener() {}
    public:
        virtual ~Listener() {}
        virtual void OnSelectPrevProgram() {}
        virtual void OnSelectNextProgram() {}
        virtual void OnSwitchToGenericEditor(bool use_generic_editor) {}
    };
    
    PluginEditorControl(wxWindow *parent,
                        bool has_editor,
                        bool use_dedicated_editor = false,
                        wxPoint pos = wxDefaultPosition,
                        wxSize size = wxDefaultSize)
    :   wxPanel(parent, wxID_ANY, pos, size)
    ,   has_editor_(has_editor)
    ,   use_dedicated_editor_(use_dedicated_editor)
    {
        btn_prev_program_ = new wxButton(this, wxID_ANY, L"Prev", wxDefaultPosition, wxSize(60, size.GetHeight()));
        btn_next_program_ = new wxButton(this, wxID_ANY, L"Next", wxDefaultPosition, wxSize(60, size.GetHeight()));
        chk_gen_editor_ = new wxCheckBox(this, wxID_ANY, L"Generic Editor", wxDefaultPosition, wxSize(120, size.GetHeight()));
        
        assert( (has_editor_ == false && use_dedicated_editor_) == false );
        
        // not implemented yet.
        btn_prev_program_->Disable();
        btn_next_program_->Disable();
        
        if(has_editor_ == false) {
            chk_gen_editor_->Disable();
        }
        if(use_dedicated_editor_) {
            chk_gen_editor_->SetValue(true);
        }
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->AddSpacer(20);
        hbox->Add(btn_prev_program_, wxSizerFlags(0).Expand());
        hbox->Add(btn_next_program_, wxSizerFlags(0).Expand());
        hbox->Add(chk_gen_editor_, wxSizerFlags(0).Expand());
        hbox->AddStretchSpacer(1);
        
        SetSizer(hbox);
        
        btn_prev_program_->Bind(wxEVT_BUTTON, [this](auto &ev) {
            listeners_.Invoke([](auto *x) {
                x->OnSelectPrevProgram();
            });
        });
        btn_next_program_->Bind(wxEVT_BUTTON, [this](auto &ev) {
            listeners_.Invoke([](auto *x) {
                x->OnSelectNextProgram();
            });
        });
        chk_gen_editor_->Bind(wxEVT_CHECKBOX, [this](auto &ev) {
            listeners_.Invoke([this](auto *x) {
                x->OnSwitchToGenericEditor(chk_gen_editor_->IsChecked());
            });
        });
    }
    
    ~PluginEditorControl()
    {}
    
    void AddListener(Listener *li) { listeners_.AddListener(li); }
    void RemoveListener(Listener const *li) { listeners_.RemoveListener(li); }
    
private:
    wxButton *btn_next_program_;
    wxButton *btn_prev_program_;
    wxCheckBox *chk_gen_editor_;
    bool has_editor_ = false;
    bool use_dedicated_editor_ = false;
    ListenerService<Listener> listeners_;
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
,   public MyApp::Vst3PluginLoadListener
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
                wxDEFAULT_FRAME_STYLE & ~(/*wxRESIZE_BORDER | */wxMAXIMIZE_BOX)
                )
    {
        MyApp::GetInstance()->AddVst3PluginLoadListener(this);
        
        on_destroy_ = on_destroy;
        
        Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) {
            contents_->CloseEditor();
            Destroy();
        });
        
        control_ = new PluginEditorControl(this, target_plugin->HasEditor());
        control_->AddListener(this);
        
        contents_ = new PluginEditorContents(this, target_plugin);
        
        genedit_ = new GenericParameterView(this, target_plugin);
        genedit_->Hide();
        
        SetMaxSize(wxSize(1000, 1000));
        SetMinSize(wxSize(10, 10));
        
        SetAutoLayout(true);
        Show(true);
    }
    
    ~PluginEditorFrame() {
        MyApp::GetInstance()->RemoveVst3PluginLoadListener(this);
        control_->RemoveListener(this);
    }
    
    bool Destroy() override
    {
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
    void OnSelectPrevProgram() override
    {
        //
    }
    
    void OnSelectNextProgram() override
    {
        //
    }
    
    void OnSwitchToGenericEditor(bool use_generic_editor) override
    {
        if(use_generic_editor) {
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
    
    void OnBeforeVst3PluginUnloaded(Vst3Plugin *plugin) override
    {
        Close();
    }
    
private:
    std::function<void()> on_destroy_;
    PluginEditorContents *contents_ = nullptr;
    PluginEditorControl *control_ = nullptr;
    GenericParameterView *genedit_ = nullptr;
    UInt32 target_unit_index_ = -1;
};

wxFrame * CreatePluginEditorFrame(wxWindow *parent,
                                  Vst3Plugin *target_plugin,
                                  std::function<void()> on_destroy)
{
    return new PluginEditorFrame(parent, target_plugin, on_destroy);
}

NS_HWM_END
