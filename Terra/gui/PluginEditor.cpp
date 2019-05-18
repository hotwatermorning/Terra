#include "PluginEditor.hpp"

#include <fmt/ostream.h>

#include "../App.hpp"
#include "./UnitData.hpp"
#include "./PCKeyboardInput.hpp"

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
    class Callback
    {
    protected:
        Callback() {}
    public:
        virtual ~Callback() {}
        
        virtual
        void OnChangeParameter(ParameterSlider *slider) = 0;
    };
    
public:
    enum { kDefaultValueMax = 1'000'000 };
    
    UInt32 ToInteger(double normalized) const {
        return std::min<UInt32>(value_max_, std::floor(normalized * (value_max_ + 1)));
    }
    
    double ToNormalized(UInt32 value) const {
        return value / (double)value_max_;
    }
    
    ParameterSlider(wxWindow *parent,
                    Vst3Plugin *plugin,
                    wxPoint pos = wxDefaultPosition,
                    wxSize size = wxDefaultSize)
    :   wxPanel(parent, wxID_ANY, pos, size)
    ,   plugin_(plugin)
    {
        UInt32 const kTitleWidth = 150;
        UInt32 const kSliderWidth = size.GetWidth();
        UInt32 const kDispWidth = 100;
           
        name_ = new wxStaticText(this, wxID_ANY, "",
                                 wxPoint(0, 0), wxSize(kTitleWidth, size.GetHeight())
                                 );
        
        slider_ = new wxSlider(this, wxID_ANY, 0, 0, 1,
                               wxPoint(0, 0), wxSize(kSliderWidth, size.GetHeight()),
                               wxSL_HORIZONTAL
                               );

        disp_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(kDispWidth, size.GetHeight()), wxTE_PROCESS_ENTER);
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->Add(name_, wxSizerFlags(0).Expand());
        hbox->Add(slider_, wxSizerFlags(1).Expand());
        hbox->Add(disp_, wxSizerFlags(0).Expand());
        
        SetSizer(hbox);
        
        slider_->Bind(wxEVT_SLIDER, [this](auto &) {
            if(GetParameterIndex() == -1) { return; }

            auto const normalized = ToNormalized(slider_->GetValue());
            auto const id = info_.id_;

            plugin_->EnqueueParameterChange(id, normalized);
            plugin_->SetParameterValueByID(id, normalized);
            
            auto str = plugin_->ValueToStringByID(id, normalized);
            disp_->SetValue(str);
            if(callback_) { callback_->OnChangeParameter(this); }
        });
        
        auto apply_text = [this](auto &e) {
            if(GetParameterIndex() == -1) { return; }
            auto const id = info_.id_;

            hwm::dout << "EVT TEXT" << std::endl;
            auto normalized = plugin_->StringToValueByID(id, disp_->GetValue().ToStdWstring());
            if(normalized >= 0) {
                plugin_->EnqueueParameterChange(id, normalized);
                slider_->SetValue(ToInteger(normalized));
            }
        };
        
        disp_->Bind(wxEVT_TEXT_ENTER, apply_text);
        disp_->Bind(wxEVT_KILL_FOCUS, apply_text);
    }

    Int32 GetParameterIndex() const
    {
        return param_index_;
    }

    void SetParameterIndex(Int32 index)
    {
        if(param_index_ == index) { return; }

        param_index_ = index;
        if(param_index_ == -1) { return; }

        info_ = plugin_->GetParameterInfoByIndex(param_index_); 

        auto value = plugin_->GetParameterValueByIndex(param_index_);
        if(info_.step_count_ >= 1) {
            value_max_ = info_.step_count_;
        }

        name_->SetLabel(info_.title_);
        name_->SetToolTip(info_.title_);
        slider_->SetMax(value_max_);
        slider_->SetMin(0);
        slider_->SetValue(ToInteger(value));

        auto init_text = plugin_->ValueToStringByIndex(param_index_, value);
        disp_->SetLabel(init_text);        
    }
    
    void UpdateSliderValue()
    {
        if(GetParameterIndex() == -1) { return; }

        auto const normalized = plugin_->GetParameterValueByIndex(param_index_);
        slider_->SetValue(ToInteger(normalized));
        auto str = plugin_->ValueToStringByIndex(param_index_, normalized);
        disp_->SetValue(str);
    }
    
    void SetCallback(Callback *callback)
    {
        callback_ = callback;
    }
    
private:
    wxStaticText *name_ = nullptr;
    wxSlider *slider_ = nullptr;
    wxTextCtrl *disp_ = nullptr;
    Vst3Plugin *plugin_ = nullptr;
    Vst3Plugin::ParameterInfo info_;
    Int32 param_index_ = -1;
    UInt32 value_max_ = kDefaultValueMax;
    Callback *callback_ = nullptr;
};

class GenericParameterView
:   public wxWindow
,   ParameterSlider::Callback
{
    constexpr static UInt32 kParameterHeight = 20;
    constexpr static UInt32 kPageSize = 3 * kParameterHeight;
    constexpr static UInt32 kSBWidth = 20;
    
public:
    GenericParameterView(wxWindow *parent, Vst3Plugin *plugin)
    :   wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(40, 40), wxCLIP_CHILDREN)
    ,   plugin_(plugin)
    {
        SetDoubleBuffered(true);
        UInt32 const num_params = plugin->GetNumParams();
        
        sb_ = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
        sb_->SetScrollbar(0, 1, num_params * kParameterHeight, 1);
        
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
        SetAutoLayout(true);
        Layout();
    }

    bool Show(bool show = true) override
    {
        if(!show) {
            for(auto *s: sliders_) {
                RemoveChild(s);
                delete s;
            }
            sliders_.clear();
        }

        return wxWindow::Show(show);
    }
    
    bool Layout() override
    {
        auto const rc = GetClientRect();
        
        sb_->SetSize(rc.GetWidth() - kSBWidth, 0, kSBWidth, rc.GetHeight());
        auto const num_params = plugin_->GetNumParams();
        
        auto tp = sb_->GetThumbPosition();
        tp = std::min<UInt32>(tp, num_params * kParameterHeight - rc.GetHeight());
        sb_->SetScrollbar(tp, rc.GetHeight(), num_params * kParameterHeight, kPageSize);
        
        tp = sb_->GetThumbPosition();
        auto const h = GetClientSize().GetHeight();

        auto const visible = [h](wxRect const &rect) { 
            return rect.GetBottom() > 0 && rect.GetTop() < h;
        };

        auto find_slider = [this](auto pred) {
            return std::find_if(sliders_.begin(), sliders_.end(), pred);
        };
         
        //! at first, cleanup all disappeared sliders.
        for(int i = 0; i < num_params; ++i) {
            auto rect = wxRect(0, i * kParameterHeight - tp, rc.GetWidth() - kSBWidth, kParameterHeight);
            if(visible(rect) == false) {
                auto found = find_slider([i](auto *s) { return s->GetParameterIndex() == i; });
                if(found != sliders_.end()) {
                    auto *s = *found;
                    s->SetParameterIndex(-1);
                    s->SetCallback(nullptr);
                    s->Hide();
                }
            }
        }

        //! and then, move existing sliders or create if needed.
        for(int i = 0; i < plugin_->GetNumParams(); ++i) {
            auto rect = wxRect(0, i * kParameterHeight - tp, rc.GetWidth() - kSBWidth, kParameterHeight);
            if(visible(rect)) {
                auto found = find_slider([i](auto *s) { return s->GetParameterIndex() == i; });
                if(found == sliders_.end()) {
                    found = find_slider([i](auto *s) { return s->GetParameterIndex() == -1; });
                }
                if(found == sliders_.end()) {
                    sliders_.push_back(new ParameterSlider(this, plugin_));
                    found = sliders_.end() - 1;
                }

                assert(found != sliders_.end());

                auto *s = *found;
                auto info = plugin_->GetParameterInfoByIndex(i);

                s->SetParameterIndex(i);
                if(info.is_program_change_) {
                    s->SetCallback(this);
                }
     
                s->SetSize(rect);
                s->Refresh();
                s->Show();
            }
        }
        
        return wxWindow::Layout();
    }
    
    void UpdateParameters()
    {
        for(auto *slider: sliders_) {
            if(slider) {
                slider->UpdateSliderValue();
            }
        }
    }
    
    void OnChangeParameter(ParameterSlider *slider) override
    {
        for(auto *slider: sliders_) {
            if(slider) {
               slider->UpdateSliderValue();
            }
        }
    }
    
private:
    std::vector<ParameterSlider *> sliders_;
    wxScrollBar *sb_;
    Vst3Plugin *plugin_ = nullptr;
};

class PluginEditorControl
:   public wxPanel
{
public:
    static constexpr Int32 kUnitChoiceWidth = 100;
    static constexpr Int32 kProgramChoiceWidth = 100;
    static constexpr Int32 kPrevProgramWidth = 40;
    static constexpr Int32 kNextProgramWidth = 40;
    static constexpr Int32 kGenericEditorCheckWidth = 120;
    static constexpr Int32 kTotalWidth
        = kUnitChoiceWidth + kProgramChoiceWidth + kPrevProgramWidth + kNextProgramWidth + kGenericEditorCheckWidth;

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
        cho_select_unit_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(kUnitChoiceWidth, size.GetHeight()));
        cho_select_program_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(kProgramChoiceWidth, size.GetHeight()));
        btn_prev_program_ = new wxButton(this, wxID_ANY, L"<<", wxDefaultPosition, wxSize(kPrevProgramWidth, size.GetHeight()));
        btn_next_program_ = new wxButton(this, wxID_ANY, L">>", wxDefaultPosition, wxSize(kNextProgramWidth, size.GetHeight()));
        chk_gen_editor_ = new wxCheckBox(this, wxID_ANY, L"Generic Editor", wxDefaultPosition, wxSize(kGenericEditorCheckWidth, size.GetHeight()));
        
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

class IPluginEditorFrame
:   public wxFrame
{
protected:
    template <class... Args>
    IPluginEditorFrame(Args&&... args)
        : wxFrame(std::forward<Args>(args)...)
    {}

public:
    virtual void OnResizePlugView() = 0;
};

class PluginEditorContents
:   public wxWindow
,   public Vst3Plugin::PlugFrameListener
{
public:
    PluginEditorContents(IPluginEditorFrame *parent,
                         Vst3Plugin *target_plugin)
    :   wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    {
        plugin_ = target_plugin;
        Show(false);

        plugin_->OpenEditor(GetHandle(), this);

        auto rc = plugin_->GetPreferredRect();
        auto csize = wxSize(rc.getWidth(), rc.getHeight());
        SetClientSize(csize);
    }

    bool Destroy() override
    {
        plugin_->CloseEditor();
        return wxWindow::Destroy();
    }

    bool Show(bool show = true) override
    {
        auto rc = plugin_->GetPreferredRect();
        auto csize = wxSize(rc.getWidth(), rc.getHeight());
        SetClientSize(csize);

        return wxWindow::Show(show);
    }

    ~PluginEditorContents()
    {
        int x = 0;
        x = 1;
    }

private:
    Vst3Plugin *plugin_ = nullptr;

    IPluginEditorFrame* GetFrame() {
        return dynamic_cast<IPluginEditorFrame*>(GetParent());
    }

    void OnResizePlugView(Steinberg::ViewRect const& rc) override
    {
        hwm::dout << "New view size: {}"_format(rc) << std::endl;

        wxSize csize(rc.getWidth(), rc.getHeight());
        SetClientSize(csize);
        GetFrame()->OnResizePlugView();
    }
};

class PluginEditorFrame
:   public IPluginEditorFrame
,   public PluginEditorControl::Listener
{

    static constexpr UInt32 kControlHeight = 20;
    wxSize const kMinFrameSize = wxSize(PluginEditorControl::kTotalWidth, kControlHeight);
    wxSize const kMaxFrameSize = wxSize(4000, 4000);

public:
    PluginEditorFrame(wxWindow *parent,
                      Vst3Plugin *target_plugin,
                      std::function<void()> on_destroy)
    :   IPluginEditorFrame(parent, wxID_ANY,
                           target_plugin->GetEffectName(),
                           wxDefaultPosition,
                           wxDefaultSize,
                           wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX))
    {
        on_destroy_ = on_destroy;

        auto sizer = new wxBoxSizer(wxVERTICAL);

        control_ = new PluginEditorControl(this, target_plugin);
        control_->GetListeners().AddListener(this);
        control_->SetSize(kMinFrameSize);

        sizer->Add(control_, wxSizerFlags(0).FixedMinSize().Expand());

        if(target_plugin->HasEditor()) {
            contents_ = new PluginEditorContents(this, target_plugin);
            genedit_ = new GenericParameterView(this, target_plugin);

            sizer->Add(contents_, wxSizerFlags(1).Expand());
            sizer->Add(genedit_, wxSizerFlags(1).Expand());
            contents_->Show();
            genedit_->Hide();
        } else {
            genedit_ = new GenericParameterView(this, target_plugin);
            sizer->Add(genedit_, wxSizerFlags(1).Expand());
            genedit_->Show();
        }

        SetSizer(sizer);
        SetAutoLayout(true);

        OnResizePlugView();
        Show(true);
        
        PCKeyboardInput::GetInstance()->ApplyTo(this);
    }

    ~PluginEditorFrame() {
        control_->GetListeners().RemoveListener(this);
    }

    bool Destroy() override
    {
        if(contents_) {
            contents_->Destroy();
            contents_ = nullptr;
        }
        
        on_destroy_();
        return wxFrame::Destroy();
    }

private:
    void OnChangeEditorType(bool use_generic_editor) override
    {
        if(use_generic_editor) {
            genedit_->UpdateParameters();
            genedit_->Show();
            contents_->Hide();
            SetMinSize(ClientToWindowSize(kMinFrameSize));
            SetMaxSize(ClientToWindowSize(kMaxFrameSize));
            SetSize(500, 500);
            auto style = (GetWindowStyle() | wxRESIZE_BORDER);
            SetWindowStyle(style);
        } else {
            genedit_->Hide();
            contents_->Show();
            auto style = (GetWindowStyle() & (~wxRESIZE_BORDER));
            SetWindowStyle(style);
            OnResizePlugView();
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

    void OnResizePlugView() override
    {
        if (!contents_) {
            return;
        }
        auto sz = contents_->GetClientSize();

        sz.IncBy(0, kControlHeight);
        sz.SetWidth(std::max<Int32>(sz.x, kMinFrameSize.x));

        auto winsize = ClientToWindowSize(sz);
        SetMinSize(wxSize(1, 1));
        SetMaxSize(winsize);
        SetMinSize(winsize);
        SetSize(winsize);
        
        Layout();
    }
};

wxFrame * CreatePluginEditorFrame(wxWindow *parent,
                                  Vst3Plugin *target_plugin,
                                  std::function<void()> on_destroy)
{
    return new PluginEditorFrame(parent, target_plugin, on_destroy);
}

NS_HWM_END
