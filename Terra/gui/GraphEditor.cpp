#include "GraphEditor.hpp"

#include <tuple>
#include <wx/dnd.h>

#include "../App.hpp"
#include "./PluginEditor.hpp"
#include "./Controls.hpp"
#include "../plugin/PluginScanner.hpp"
#include "./Util.hpp"
#include "../resource/ResourceHelper.hpp"
#include "../file/ProjectObjectTable.hpp"
#include "../misc/MathUtil.hpp"
#include "../misc/Range.hpp"

NS_HWM_BEGIN

BrushPenSet bps_audio_pin_ = {
    { HSVToColour(0.2, 0.6, 0.7), HSVToColour(0.2, 0.6, 0.6) },
    { HSVToColour(0.2, 0.6, 0.95), HSVToColour(0.2, 0.6, 0.85) },
    { HSVToColour(0.2, 0.6, 0.9), HSVToColour(0.2, 0.6, 0.8)}
};

BrushPenSet bps_midi_pin_ = {
    { HSVToColour(0.5, 0.6, 0.7), HSVToColour(0.5, 0.6, 0.6) },
    { HSVToColour(0.5, 0.6, 0.95), HSVToColour(0.5, 0.6, 0.85) },
    { HSVToColour(0.5, 0.6, 0.9), HSVToColour(0.5, 0.6, 0.8)}
};

BrushPen const kPinShadow = { HSVToColour(0.0, 0.0, 0.0, 0.5) };

wxPen const kAudioLine = wxPen(HSVToColour(0.2, 0.8, 1.0, 0.7), 2, wxPENSTYLE_SOLID);
wxPen const kMidiLine = wxPen(HSVToColour(0.5, 0.8, 1.0, 0.7), 2, wxPENSTYLE_SOLID);
wxPen const kScissorLine = wxPen(HSVToColour(0.5, 0.0, 0.7, 0.7), 2, wxPENSTYLE_SHORT_DASH);

auto const kNodeShadow = BrushPen(HSVToColour(0.0, 0.0, 0.0, 0.3));
int const kShadowRadius = 5;
int const kNodeRound = 3;

BrushPen const kNodeColor = BrushPen(HSVToColour(0.0, 0.0, 0.9));
BrushPen const kNodeColorHavingFocus = BrushPen(HSVToColour(0.0, 0.0, 0.9), HSVToColour(0.7, 1.0, 0.9));

wxColour const kGraphBackground(HSVToColour(0.0, 0.0, 0.1));

wxSize const kDefaultNodeSize = { 200, 40 };

wxSize const kNodeButtonSize = { 14, 14 };
Int32 kNodeAlignmentSize = 10;
int const kPinRadius = 6;
wxSize const kLabelSize = { kDefaultNodeSize.GetWidth(), kDefaultNodeSize.GetHeight() - (kPinRadius * 4) };

class NodeComponent
:   public IRenderableWindow<wxWindow>
{
    using base_type = IRenderableWindow<wxWindow>;
public:
    struct Callback {
        virtual ~Callback() {}
        virtual void OnRequestToUnload(NodeComponent *nc) = 0;
        //! ptはparent基準
        virtual void OnMouseMove(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) = 0;
        //! ptはparent基準
        virtual void OnReleaseMouse(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) = 0;
        
        virtual void OnRaised(NodeComponent *nc) = 0;
    };
    
    struct Pin
    {
        enum class Type { kAudio, kMidi };
        
        static Pin MakeAudioInput(int index) { return Pin { Type::kAudio, BusDirection::kInputSide, index }; }
        static Pin MakeAudioOutput(int index) { return Pin { Type::kAudio, BusDirection::kOutputSide, index }; }
        static Pin MakeMidiInput(int index) { return Pin { Type::kMidi, BusDirection::kInputSide, index }; }
        static Pin MakeMidiOutput(int index) { return Pin { Type::kMidi, BusDirection::kOutputSide, index }; }
     
        Type type_;
        BusDirection dir_;
        int index_;
        
        bool IsAudioPin() const { return type_ == Type::kAudio; }
        bool IsMidiPin() const { return type_ == Type::kMidi; }
        
        bool operator==(Pin const &rhs) const
        {
            auto to_tuple = [](auto &self) {
                return std::tie(self.type_, self.dir_, self.index_);
            };
            
            return to_tuple(*this) == to_tuple(rhs);
        }
        
        bool operator!=(Pin const &rhs) const
        {
            return !(*this == rhs);
        }
    };
    
public:
    const double kVolumeSliderScale = 1000.0;
    wxSize node_size_ = kDefaultNodeSize;
    
    NodeComponent(wxWindow *parent, GraphProcessor::Node *node, Callback *callback)
    :   base_type()
    ,   node_(node)
    ,   callback_(callback)
    ,   node_size_(kDefaultNodeSize)
    {
        SetLabel(node->GetProcessor()->GetName());
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Create(parent, wxID_ANY, wxDefaultPosition, node_size_, wxTRANSPARENT_WINDOW);

        UseDefaultPaintMethod(false);
        ImageAsset button_images = ImageAsset(GetResourcePath(L"graph/node_buttons.png"), 2, 4);
        btn_open_editor_ = new ImageButton(this, true, button_images.GetImage(0, 0), button_images.GetImage(0, 1), button_images.GetImage(0, 2), button_images.GetImage(0, 3));
        btn_open_editor_->UseDefaultPaintMethod(false);
        btn_open_editor_->SetClientSize(kNodeButtonSize);
        btn_open_editor_->SetMinClientSize(kNodeButtonSize);
        
        btn_open_editor_->Bind(wxEVT_TOGGLEBUTTON, [this](auto &ev) {
            if(IsEditorOpened()) {
                CloseEditor();
            } else {
                OpenEditor();
            }
        });
        
        btn_open_mixer_ = new ImageButton(this, true, button_images.GetImage(1, 0), button_images.GetImage(1, 1), button_images.GetImage(1, 2), button_images.GetImage(1, 3));
        btn_open_mixer_->UseDefaultPaintMethod(false);
        btn_open_mixer_->SetClientSize(kNodeButtonSize);
        btn_open_mixer_->SetMinClientSize(kNodeButtonSize);
        
        btn_open_mixer_->Bind(wxEVT_TOGGLEBUTTON, [this](auto &ev) {
            auto size = GetClientSize();
            if(btn_open_mixer_->IsPushed()) {
                size.IncBy(0, 20);
                node_size_.IncBy(0, 20);
                SetClientSize(size);
                detail_box_->ShowItems(true);
            } else {
                size.DecBy(0, 20);
                node_size_.DecBy(0, 20);
                SetClientSize(size);
                detail_box_->ShowItems(false);
            }
            Layout();
        });
        
        int min_value = node_->GetProcessor()->GetVolumeLevelMin();
        int max_value = node_->GetProcessor()->GetVolumeLevelMax();
        sl_volume_ = new wxSlider(this, wxID_ANY,
                                  max_value * kVolumeSliderScale,
                                  min_value * kVolumeSliderScale,
                                  max_value * kVolumeSliderScale,
                                  wxDefaultPosition,
                                  wxSize(100, 20));
        sl_volume_->Bind(wxEVT_SLIDER, [this](auto &ev) { OnVolumeSliderChanged(); });

        bool enable_editor_button = false;
        //! There's always generic plugin views for every plugins even if the plugin provides no editor ui.
        if(auto p = dynamic_cast<Vst3AudioProcessor *>(node_->GetProcessor().get())) {
            enable_editor_button = true;
        }
        btn_open_editor_->Enable(enable_editor_button);
        
        if(node_->GetProcessor()->GetAudioChannelCount(BusDirection::kOutputSide) == 0 &&
           node_->GetProcessor()->GetAudioChannelCount(BusDirection::kInputSide) == 0)
        {
            btn_open_mixer_->Enable(false);
        }
        
        lbl_plugin_name_ = new Label(this);
        lbl_plugin_name_->UseDefaultPaintMethod(false);
        lbl_plugin_name_->SetText(node->GetProcessor()->GetName());
        lbl_plugin_name_->SetAlignment(wxALIGN_CENTRE);
        lbl_plugin_name_->SetMinSize(kLabelSize);
        
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        
        {
            auto property_box = new wxBoxSizer(wxVERTICAL);
            property_box->AddSpacer(kPinRadius * 2);
            property_box->Add(lbl_plugin_name_, wxSizerFlags(1).Expand());
            
            {
                detail_box_ = new wxBoxSizer(wxHORIZONTAL);
                detail_box_->Add(sl_volume_, wxSizerFlags(1).Expand());
                detail_box_->ShowItems(false);
                property_box->Add(detail_box_, wxSizerFlags(1).Expand());
            }
            
            property_box->AddSpacer(kPinRadius * 2);
            hbox->Add(property_box, wxSizerFlags(1).Expand());
        }
        
        {
            auto command_box = new wxBoxSizer(wxVERTICAL);
            command_box->Add(btn_open_editor_, wxSizerFlags(0));
            command_box->Add(btn_open_mixer_, wxSizerFlags(0));
            hbox->Add(command_box, wxSizerFlags(0).Expand());
        }
        
        SetSizer(hbox);

        SetAutoLayout(true);
        Layout();
        
        Bind(wxEVT_MOVE, [this](auto &ev) {
            GetParent()->Refresh();
        });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &ev) {
            //hwm::dout << "Left Down: " << node_->GetProcessor()->GetName() << std::endl;
            OnLeftDown(ev);
        });        
        Bind(wxEVT_LEFT_UP, [this](auto &ev) {
            //hwm::dout << "Left Up: " << node_->GetProcessor()->GetName() << std::endl;
            OnLeftUp(ev);
        });
        Bind(wxEVT_MOTION, [this](auto &ev) {
            //hwm::dout << "Mouse Move: " << node_->GetProcessor()->GetName() << std::endl;
            OnMouseMove(ev);
        });
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) {
            //hwm::dout << "Right Up: " << node_->GetProcessor()->GetName() << std::endl;
            OnRightUp(ev);
        });
        Bind(wxEVT_SET_FOCUS, [this](auto &ev) {
            hwm::dout << "Set focus: " << node_->GetProcessor()->GetName() << std::endl;
            //OnCaptureLost();
            Refresh();
        });
        Bind(wxEVT_KILL_FOCUS, [this](auto &ev) {
            hwm::dout << "Kill Focus: " << node_->GetProcessor()->GetName() << std::endl;
            OnCaptureLost();
            Refresh();
        });
        Bind(wxEVT_CHILD_FOCUS, [this](auto &ev) {
            hwm::dout << "Child Focus: " << node_->GetProcessor()->GetName() << std::endl;
            //SetFocus();
            //Raise();
        });
        Bind(wxEVT_LEAVE_WINDOW, [this](auto &ev) {
            //hwm::dout << "Leave Window: " << node_->GetProcessor()->GetName() << std::endl;
            OnMouseLeave(ev);
        });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](auto &ev) {
            hwm::dout << "Mouse Capture Lost: " << node_->GetProcessor()->GetName() << std::endl;
            ev.Skip();
        });
        Bind(wxEVT_MOUSE_CAPTURE_CHANGED, [this](auto &ev) {
            hwm::dout << "Mouse Capture Changed: " << node_->GetProcessor()->GetName() << std::endl;
            ev.Skip();
        });
        Bind(wxEVT_KEY_DOWN, [](auto &ev) { ev.ResumePropagation(100); ev.Skip(); });
        Bind(wxEVT_KEY_UP, [](auto &ev) { ev.ResumePropagation(100); ev.Skip(); });
        
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) { OnRightUp(ev); });
    }
    
    bool Layout() override
    {
        auto const client_size = GetClientSize();
        auto image_size = client_size;
        image_size.IncBy(kShadowRadius * 2);
        
        auto image = wxImage(image_size);
        image.SetAlpha();
        ClearImage(image);

        auto bitmap = wxBitmap(image, 32);
        wxMemoryDC memory_dc(bitmap);
		wxGCDC dc(memory_dc);
        
        BrushPen bp { HSVToColour(0, 0.6, 1.0, 0.0) };
        bp.ApplyTo(dc);
        dc.SetBackground(bp.brush_);
        dc.Clear();
        dc.DrawRoundedRectangle(wxPoint{}, image.GetSize(), kNodeRound);
        
        kNodeShadow.ApplyTo(dc);

        dc.DrawRectangle(wxPoint{kShadowRadius, kShadowRadius}, client_size);
        memory_dc.SelectObject(wxNullBitmap);
        
        image = bitmap.ConvertToImage();
        image = image.Blur(kShadowRadius);
        shadow_ = image;
        
        return base_type::Layout();
    }
    
    ~NodeComponent()
    {
        if(editor_frame_) { editor_frame_->Destroy(); }
    }
    
    void Raise() override
    {
        hwm::wdout << L"Raised: " << lbl_plugin_name_->GetText() << std::endl;
        SetFocus();
        if(HasFocus() == false) {
            hwm::wdout << L"Failed to focus this window: " << this->GetLabel() << std::endl;
        }
        callback_->OnRaised(this);
        base_type::Raise();
        Refresh();
    }
    
    void Lower() override
    {
        hwm::dout << "Lower: " << lbl_plugin_name_->GetText() << std::endl;
        base_type::Lower();
    }
    
    void OpenEditor()
    {
        if(editor_frame_) { return; }
        
        auto proc = dynamic_cast<Vst3AudioProcessor *>(node_->GetProcessor().get());
        if(!proc) { return; }
        
        editor_frame_ = CreatePluginEditorFrame(this,
                                                proc->plugin_.get(),
                                                [this] {
                                                    editor_frame_ = nullptr;
                                                    btn_open_editor_->SetPushed(false);
                                                });
        btn_open_editor_->SetPushed(true);
    }
    
    void CloseEditor()
    {
        if(editor_frame_ == nullptr) { return; }
        
        editor_frame_->Destroy();
        editor_frame_ = nullptr;
    }
    
    bool IsEditorOpened() const
    {
        return editor_frame_;
    }
    
    void doRender(wxDC &dc) override
    {
        dc.SetBrush(*wxBLUE_BRUSH);
        dc.SetPen(*wxGREEN_PEN);
        dc.SetBackground(*wxRED_BRUSH);
        dc.DrawRectangle(GetClientRect().Deflate(20));

        auto bitmap = wxBitmap(shadow_, 32);
        dc.DrawBitmap(bitmap, wxPoint{-kShadowRadius, -kShadowRadius});
        
        if(HasFocus()) {
            kNodeColorHavingFocus.ApplyTo(dc);
        } else {
            kNodeColor.ApplyTo(dc);
        }
        
        auto rc_bg = wxRect(wxPoint{}, node_size_);
        dc.DrawRoundedRectangle(rc_bg, kNodeRound);
        
        auto const p = node_->GetProcessor().get();
        
        auto pt = ScreenToClient(::wxGetMousePosition());
        
        int const num_ai = p->GetAudioChannelCount(BusDirection::kInputSide);
        int const num_ao = p->GetAudioChannelCount(BusDirection::kOutputSide);
        int const num_mi = p->GetMidiChannelCount(BusDirection::kInputSide);
        int const num_mo = p->GetMidiChannelCount(BusDirection::kOutputSide);
        
        auto hover_pin = GetPin(pt);

        auto draw_pin = [&, this](auto pin, auto &brush_pen_set) {
            auto const center = GetPinCenter(pin);
            
            kPinShadow.ApplyTo(dc);
            dc.DrawCircle(center + wxPoint(1, 1), kPinRadius - 1);
            
            if(pin == hover_pin) {
                brush_pen_set.hover_.ApplyTo(dc);
            } else if(pin == selected_pin_) {
                brush_pen_set.selected_.ApplyTo(dc);
            } else {
                brush_pen_set.normal_.ApplyTo(dc);
            }
            
            dc.DrawCircle(center, kPinRadius-1);
        };
        
        for(int i = 0; i < num_ai; ++i) { draw_pin(Pin::MakeAudioInput(i), bps_audio_pin_); }
        for(int i = 0; i < num_ao; ++i) { draw_pin(Pin::MakeAudioOutput(i), bps_audio_pin_); }
        for(int i = 0; i < num_mi; ++i) { draw_pin(Pin::MakeMidiInput(i), bps_midi_pin_); }
        for(int i = 0; i < num_mo; ++i) { draw_pin(Pin::MakeMidiOutput(i), bps_midi_pin_); }
        
        for(auto child: GetChildren()) {
            if(auto p = dynamic_cast<IRenderableWindowBase *>(child)) {
                p->RenderWithParentDC(dc);
            }
        }
    }
    
    void OnLeftDown(wxMouseEvent& ev)
    {
        selected_pin_ = GetPin(ev.GetPosition());

        Raise();
        Refresh();
        CaptureMouse();
        
        if(selected_pin_) {
            pin_drag_begin_ = ev.GetPosition() + GetPosition();
            delta_ = std::nullopt;
        } else {
            wxPoint pos = ::wxGetMousePosition();
            wxPoint origin = this->GetPosition();
            delta_ = pos - origin;
            pin_drag_begin_ = std::nullopt;
        }
    }
    
    void OnLeftUp(wxMouseEvent& ev)
    {
        if (HasCapture()) {
            ReleaseMouse();
            OnCaptureLost();
            
            if(pin_drag_begin_) {
                auto pt = ev.GetPosition() + GetPosition();
                callback_->OnReleaseMouse(this, *pin_drag_begin_, pt);
            }
        }
    }
    
    void OnMouseMove(wxMouseEvent& ev)
    {
        if(!HasCapture()) {
            Refresh();
            return;
        }
        
        if (ev.Dragging() && ev.LeftIsDown()) {
            
            if(delta_) {
                wxPoint pos = ::wxGetMousePosition();
                MoveConstrained(pos - *delta_);
            } else if(pin_drag_begin_) {
                auto pt = ev.GetPosition() + GetPosition();
                callback_->OnMouseMove(this, *pin_drag_begin_, pt);
            }
        }
    }
    
    void OnRightUp(wxMouseEvent &ev)
    {
        ShowPopup();
    }
    
    void OnMouseLeave(wxMouseEvent &ev)
    {
        if(!HasCapture()) {
            OnCaptureLost();
            return;
        }
    }
    
    void OnCaptureLost()
    {
        selected_pin_ = std::nullopt;
        callback_->OnMouseMove(this, wxPoint(-1, -1), wxPoint(-1, -1));
        Refresh();
    }
    
    // ptは、ウィンドウ相対座標。
    // 見つからないときはnulloptが返る。
    std::optional<Pin> GetPin(wxPoint pt) const
    {
        auto get_impl = [this](wxPoint pt, Pin::Type type, BusDirection dir) -> std::optional<Pin> {
            
            int num = 0;
            if(type == Pin::Type::kAudio) {
                num = node_->GetProcessor()->GetAudioChannelCount(dir);
            } else {
                num = node_->GetProcessor()->GetMidiChannelCount(dir);
            }
            
            for(int i = 0; i < num; ++i) {
                Pin pin { type, dir, i };
                auto center = GetPinCenter(pin);
                auto rc = wxRect(center.x - kPinRadius, center.y - kPinRadius,
                                 kPinRadius * 2, kPinRadius * 2);
                if(rc.Contains(pt)) {
                    return pin;
                }
            }
            
            return std::nullopt;
        };
        
        if(auto pin = get_impl(pt, Pin::Type::kAudio, BusDirection::kInputSide)) {
            return pin;
        } else if(auto pin = get_impl(pt, Pin::Type::kAudio, BusDirection::kOutputSide)) {
            return pin;
        } else if(auto pin = get_impl(pt, Pin::Type::kMidi, BusDirection::kInputSide)) {
            return pin;
        } else if(auto pin = get_impl(pt, Pin::Type::kMidi, BusDirection::kOutputSide)) {
            return pin;
        } else {
            return std::nullopt;
        }
    }
    
    wxPoint GetPinCenter(Pin pin) const
    {
        auto rect = GetClientRect();
        
        int const na = node_->GetProcessor()->GetAudioChannelCount(pin.dir_);
        int const nm = node_->GetProcessor()->GetMidiChannelCount(pin.dir_);
        int const width_audio_pins = rect.GetWidth();
        
        double const width_audio_pin = width_audio_pins / (double)(na + nm);
        
        int index = 0;
        if(pin.type_ == Pin::Type::kAudio) {
            index = pin.index_;
        } else {
            index = na + pin.index_;
        }
        
        return wxPoint(width_audio_pin * (index + 0.5),
                       (pin.dir_ == BusDirection::kInputSide ? kPinRadius : rect.GetHeight() - kPinRadius)
                       );
    }
    
    void MoveConstrained(wxPoint pt)
    {
        pt.x = (pt.x / kNodeAlignmentSize) * kNodeAlignmentSize;
        pt.y = (pt.y / kNodeAlignmentSize) * kNodeAlignmentSize;
        
        pt.x = std::max<int>(pt.x, 0);
        pt.y = std::max<int>(pt.y, 0);
        pt.x = std::min<int>(pt.x, GetParent()->GetClientSize().x - GetClientSize().x);
        pt.y = std::min<int>(pt.y, GetParent()->GetClientSize().y - GetClientSize().y);
        Move(pt);
        GetParent()->Refresh();
    }
        
     enum {
         kID_Disconnect_Inputs = wxID_HIGHEST + 1,
         kID_Disconnect_Outputs,
         kID_RequestToUnload,
     };
     
    void ShowPopup()
    {
        wxMenu menu;
        //menu.Append(kID_Disconnect_Inputs, L"&Disconnect All Inputs", L"&Disconnect All Inputs");
        //menu.Append(kID_Disconnect_Outputs, L"&Disconnect All Outputs", L"&Disconnect All Outputs");
        //menu.AppendSeparator();
        menu.Append(kID_RequestToUnload, "&Unload\tCTRL-u", "Unload this plugin");
        
        menu.Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) {
            if(ev.GetId() == kID_RequestToUnload) { callback_->OnRequestToUnload(this); }
        });
        
        PopupMenu(&menu);
    }
    
    void OnVolumeSliderChanged()
    {
        node_->GetProcessor()->SetVolumeLevel(sl_volume_->GetValue() / kVolumeSliderScale);
    }
    
    GraphProcessor::Node *node_ = nullptr;
    ImageButton     *btn_open_editor_ = nullptr;
    ImageButton     *btn_open_mixer_ = nullptr;
    Label           *lbl_plugin_name_ = nullptr;
    wxFrame         *editor_frame_ = nullptr;
    wxBoxSizer      *detail_box_ = nullptr;
    wxSlider        *sl_volume_ = nullptr;
    std::optional<Pin> selected_pin_;
    std::optional<wxPoint> delta_; // window 移動
    std::optional<wxPoint> pin_drag_begin_; // pin選択
    std::function<void()> request_to_unload_;
    Callback *callback_ = nullptr;
    wxImage shadow_;
};

bool Intersect(wxPoint a1, wxPoint a2, wxPoint b1, wxPoint b2)
{
    int a = (a2.x - a1.x) * (b1.y - a1.y) - (a2.y - a1.y) * (b1.x - a1.x);
    int b = (a2.x - a1.x) * (b2.y - a1.y) - (a2.y - a1.y) * (b2.x - a1.x);
    
    int c = (b2.x - b1.x) * (a1.y - b1.y) - (b2.y - b1.y) * (a1.x - b1.x);
    int d = (b2.x - b1.x) * (a2.y - b1.y) - (b2.y - b1.y) * (a2.x - b1.x);
    
    auto is_same_sign = [](auto p, auto q) { return (p >= 0) == (q >= 0); };
    return !is_same_sign(a, b) && !is_same_sign(c, d);
}

template<class... Args>
GraphEditor::GraphEditor(Args&&... args)
:   wxPanel(std::forward<Args>(args)...)
{}

GraphEditor::~GraphEditor()
{}

class GraphEditorImpl
:   public GraphEditor
,   public NodeComponent::Callback
,   public MyApp::ChangeProjectListener
,   public GraphProcessor::Listener
{
    struct DropTarget
    :    public wxFileDropTarget
    {
        DropTarget(GraphEditorImpl *owner)
        :   owner_(owner)
        {}
        
        bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames) override
        {
            return owner_->OnDropFiles(x, y, filenames);
        }
        
    private:
        GraphEditorImpl *owner_;
    };
public:
    wxCursor scissors_;
    GraphEditorImpl(wxWindow *parent)
        : GraphEditor()
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        SetDoubleBuffered(true);
        SetDropTarget(new DropTarget(this));
        wxImage img;

        img.LoadFile(GetResourcePath(L"/cursor/scissors.png"));
        int const cursor_x = 24;
        int const cursor_y = 24;

        img = img.Scale(cursor_x, cursor_y, wxIMAGE_QUALITY_HIGH);
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, cursor_x / 2);
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, cursor_y / 2);

        scissors_ = wxCursor(img);

        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) { OnRightUp(ev); });
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { OnKeyDown(ev); ev.ResumePropagation(100); ev.Skip(); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { OnKeyUp(ev); ev.ResumePropagation(100); ev.Skip(); });
        Bind(wxEVT_KILL_FOCUS, [this](auto &ev) { OnKillFocus(); });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](auto &ev) { OnReleaseMouse(); });
        slr_change_project_.reset(MyApp::GetInstance()->GetChangeProjectListeners(), this);

        SetBackgroundColour(kGraphBackground);
        SetAutoLayout(true);
        Layout();

        timer_.Bind(wxEVT_TIMER, [this](auto &ev) {
            std::for_each(node_components_.begin(),
                          node_components_.end(),
                          [](auto &nc) { nc->Refresh(); }
                          );
        });
        timer_.Start(100);
    }

    wxTimer timer_;
    
    ~GraphEditorImpl()
    {
        RemoveGraph();
        slr_change_project_.reset();
    }
    
    bool Layout() override
    {
        back_buffer_ = GraphicsBuffer(GetClientSize());
        return GraphEditor::Layout();
    }
    
    NodeComponent * FindNodeComponent(GraphProcessor::Node const *node)
    {
        auto found = std::find_if(node_components_.begin(),
                                  node_components_.end(),
                                  [node](auto &nc) { return nc->node_ == node; });
        if(found == node_components_.end()) {
            return nullptr;
        } else {
            return found->get();
        }
    };
    
    void OnChangeCurrentProject(Project *old_pj, Project *new_pj) override
    {
        RemoveGraph();
        
        if(new_pj) {
            SetGraph(new_pj->GetGraph());
        }
    }
    
    void OnBeforeSaveProject(Project *pj, schema::Project &schema) override
    {
        assert(pj && (&pj->GetGraph() == graph_));
        assert(schema.has_graph());
        
        auto &schema_nodes = *schema.mutable_graph()->mutable_nodes();
        
        for(auto const &nc: node_components_) {
            auto const id = nc->node_->GetID();
            auto found = std::find_if(schema_nodes.begin(), schema_nodes.end(),
                                      [id](auto const &node) { return node.id() == id; });
            if(found == schema_nodes.end()) { continue; }
            auto *pos = found->mutable_pos();
            pos->set_x(nc->GetPosition().x);
            pos->set_y(nc->GetPosition().y);
        }
    }
    
    void OnAfterLoadProject(Project *pj, schema::Project const &schema) override
    {
        if(!schema.has_graph()) { return; }
        
        assert(pj && (&pj->GetGraph() == graph_));
        auto objects = ProjectObjectTable::GetInstance();
        
        auto &new_nodes = objects->nodes_;
        auto &schema_nodes = schema.graph().nodes();
        
        auto const width = GetClientSize().GetWidth();
        auto const height = GetClientSize().GetHeight();
        
        for(auto &schema_node: schema_nodes) {
            auto found_node = new_nodes.Find(schema_node.id());
            if(found_node == nullptr) { continue; }
            
            auto nc = FindNodeComponent(found_node);
            if(nc == nullptr) { continue; }
            
            wxPoint pos{};
            if(schema_node.has_pos()) {
                auto node_size = nc->node_size_;
                pos.x = Clamp<int>(schema_node.pos().x(), 0, width - node_size.GetWidth());
                pos.y = Clamp<int>(schema_node.pos().y(), 0, height - node_size.GetHeight());
            }
            
            nc->SetPosition(pos);
        }
        
        Layout();
    }
    
    void OnLeftDown(wxMouseEvent const &ev)
    {
        if(ev.GetModifiers() == wxMOD_SHIFT) {
            LineSetting ls;
            ls.begin_ = ev.GetPosition();
            ls.end_ = ls.begin_;
            ls.pen_ = kScissorLine;
            dragging_line_ = ls;
            CaptureMouse();
            SetCursor(scissors_);
        }
    }
    
    void OnLeftUp(wxMouseEvent const &ev)
    {
        if(HasCapture()) {
            assert(dragging_line_);
            RemoveConnection(dragging_line_->begin_, dragging_line_->end_);
            ReleaseMouse();
            OnReleaseMouse();
            Refresh();
        }
    }
    
    std::vector<GraphProcessor::ConnectionPtr>
    FindConnectionsToRemove(wxPoint begin, wxPoint end,
                           NodeComponent::Pin::Type pin_type)
    {
        std::vector<GraphProcessor::ConnectionPtr> ret;
        
        for(auto const &nc: node_components_) {
            auto *node = nc->node_;
            
            std::vector<GraphProcessor::ConnectionPtr> conns;
            if(pin_type == NodeComponent::Pin::Type::kAudio) {
                auto tmp = node->GetAudioConnections(BusDirection::kOutputSide);
                conns.assign(tmp.begin(), tmp.end());
            } else {
                auto tmp = node->GetMidiConnections(BusDirection::kOutputSide);
                conns.assign(tmp.begin(), tmp.end());
            }
            
            for(auto conn: conns) {
                auto *node_down = conn->downstream_;
                auto nc2 = FindNodeComponent(node_down);
                if(!nc2) { continue; }
                
                auto upstream_pin_center
                = nc->GetPinCenter(NodeComponent::Pin { pin_type, BusDirection::kOutputSide, (Int32)conn->upstream_channel_index_ })
                + nc->GetPosition();
                
                auto downstream_pin_center
                = nc2->GetPinCenter(NodeComponent::Pin { pin_type, BusDirection::kInputSide, (Int32)conn->downstream_channel_index_ })
                + nc2->GetPosition();
                
                if(Intersect(begin, end, upstream_pin_center, downstream_pin_center)) {
                    ret.push_back(conn);
                }
            }
        }
        
        return ret;
    }
    
    void RemoveConnection(wxPoint begin, wxPoint end)
    {
        for(auto const &conn: FindConnectionsToRemove(begin, end, NodeComponent::Pin::Type::kAudio)) {
            graph_->Disconnect(conn);
        }
        
        for(auto const &conn: FindConnectionsToRemove(begin, end, NodeComponent::Pin::Type::kMidi)) {
            graph_->Disconnect(conn);
        }
        
        Refresh();
    }
    
    void OnMouseMove(wxMouseEvent const &ev)
    {
        if(ev.Dragging()) {
            if(dragging_line_) {
                dragging_line_->end_ = ev.GetPosition();
                Refresh();
            }
        }
    }
    
    void OnRightUp(wxMouseEvent const &ev)
    {
        auto menu_plugins = new wxMenu();
        const int kPluginIDStart = 1000;
        
        auto ps = PluginScanner::GetInstance();
        auto descs = ps->GetPluginDescriptions();
        
        static auto get_category_number = [](auto const &desc) {
            return IsEffectPlugin(desc) + IsInstrumentPlugin(desc) * 2;
        };
        
        std::sort(descs.begin(),
                  descs.end(),
                  [](auto const &lhs, auto &rhs) {
                      return
                      std::tuple<int, std::string const &>(get_category_number(lhs), lhs.name())
                      <
                      std::tuple<int, std::string const &>(get_category_number(rhs), rhs.name());
                  });
        
        for(int i = 0; i < descs.size(); ++i) {
            auto const &desc = descs[i];
            std::string plugin_name;
            if(IsEffectPlugin(desc) && IsInstrumentPlugin(desc)) { plugin_name = "[Fx|Inst] "; }
            else if(IsEffectPlugin(desc)) { plugin_name = "[Fx] "; }
            else if(IsInstrumentPlugin(desc)) { plugin_name = "[Inst] "; }
            else { plugin_name = "[Unknown] "; }
        
            plugin_name += desc.name();
            menu_plugins->Append(kPluginIDStart + i, plugin_name);
        }
        
        wxMenu menu;
        menu.AppendSubMenu(menu_plugins, "Load Plugin");
        
        menu.Bind(wxEVT_COMMAND_MENU_SELECTED, [&, this, pos = ev.GetPosition()](auto &ev) {
            auto const id = ev.GetId();
            if(id >= kPluginIDStart) {
                auto const index = id - kPluginIDStart;
                assert(index < descs.size());
                AddNode(descs[index], pos);
            }
        });
        
        PopupMenu(&menu, ev.GetPosition());
    }
    
    void AddNode(schema::PluginDescription const &desc, wxPoint pt)
    {
        auto app = MyApp::GetInstance();
        std::unique_ptr<Vst3Plugin> plugin = app->CreateVst3Plugin(desc);
        
        if(!plugin) {
            wxMessageBox("Failed to load {}"_format(desc.name()));
            return;
        }
        
        auto proc = std::make_shared<Vst3AudioProcessor>(desc, std::move(plugin));
        auto node = graph_->AddNode(proc);

        auto &back = node_components_.back();
        
        assert(back->node_ == node.get());
        back->Show(true);
        back->MoveConstrained(pt);
        back->Raise();
    }

    //! return true if removed.
    //! return false if not found.
    bool RemoveNode(Processor const *proc)
    {
        auto node = graph_->GetNodeOf(proc);
        if(!node) { return false; }

        graph_->RemoveNode(node);
        return true;
    }
    
    void SetGraph(GraphProcessor &graph)
    {
        RemoveGraph(); // if exists
        
        // construct node components
        graph_ = &graph;
        auto nodes = graph_->GetNodes();
        
        for(auto node: nodes) {
            auto nc = std::make_unique<NodeComponent>(this, node.get(), this);
            nc->UseDefaultPaintMethod(false);
            node_components_.push_back(std::move(nc));
        }
        
        graph_->GetListeners().AddListener(this);
        //screen_->Raise();
        Refresh();
    }
    
    void RemoveGraph()
    {
        if(!graph_) { return; }
        
        graph_->GetListeners().RemoveListener(this);

        for(auto &node: node_components_) {
            RemoveChild(node.get());
            node->Destroy();
            node.release();
        }
        
        node_components_.clear();
        graph_ = nullptr;
        
        Refresh();
    }
    
    void OnRequestToUnload(NodeComponent *nc) override
    {
        this->CallAfter([proc = nc->node_->GetProcessor().get(), this] {
            RemoveNode(proc);
        });
    }
    
    //! ptはparent基準
    void OnMouseMove(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) override
    {
        auto pin_begin = nc->GetPin(pt_begin - nc->GetPosition());
        if(!pin_begin) {
            if(dragging_line_) { Refresh(); }
            dragging_line_ = std::nullopt;
            return;
        }
        
        LineSetting ls;
        ls.begin_ = pt_begin;
        ls.end_ = pt_end;
        ls.pen_ = (pin_begin->IsAudioPin() ? kAudioLine : kMidiLine);
        ls.pen_.SetStyle(wxPENSTYLE_DOT);
        dragging_line_ = ls;
        Refresh();
    }
    
    void OnReleaseMouse()
    {
        dragging_line_ = std::nullopt;
        SetCursor(wxNullCursor);
    }
    
    //! ptはparent基準
    void OnReleaseMouse(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) override
    {
        auto pin_begin = nc->GetPin(pt_begin - nc->GetPosition());
        if(!pin_begin) { return; }
        
        BusDirection const opposite_dir =
        (pin_begin->dir_ == BusDirection::kInputSide) ? BusDirection::kOutputSide : BusDirection::kInputSide;
        
        for(auto &node: node_components_) {
            auto const pt = pt_end - node->GetPosition();
            auto const opposite_pin = node->GetPin(pt);
            if(!opposite_pin) { continue; }
            if(opposite_pin->dir_ != opposite_dir) { return; }
            if(pin_begin->type_ != opposite_pin->type_) { return; }
            
            GraphProcessor::Node *nup = nullptr;
            GraphProcessor::Node *ndown = nullptr;
            int chup = 0;
            int chdown = 0;
            
            if(pin_begin->dir_ == BusDirection::kOutputSide) {
                nup = nc->node_;
                ndown = node->node_;
                chup = pin_begin->index_;
                chdown = opposite_pin->index_;
            } else {
                ndown = nc->node_;
                nup = node->node_;
                chdown = pin_begin->index_;
                chup = opposite_pin->index_;
            }
            
            if(pin_begin->type_ == NodeComponent::Pin::Type::kAudio) {
                graph_->ConnectAudio(nup, ndown, chup, chdown);
            } else {
                graph_->ConnectMidi(nup, ndown, chup, chdown);
            }
            
            Refresh();
            break;
        }
    }
    
    void OnRaised(NodeComponent *p) override
    {
        auto found = find_if(node_components_.begin(),
                             node_components_.end(),
                             [p](auto &elem) { return elem.get() == p; });
        assert(found != node_components_.end());
        
        if(found == node_components_.end() - 1) { return; }
        
        auto tmp = std::move(*found);
        node_components_.erase(found);
        node_components_.push_back(std::move(tmp));
        Refresh();
    }
    
    void OnKeyDown(wxKeyEvent &ev)
    {
        if(ev.GetModifiers() == wxMOD_SHIFT) {
            SetCursor(scissors_);
        }
    }
    
    void OnKeyUp(wxKeyEvent &ev)
    {
        if(!dragging_line_ && ev.GetModifiers() != wxMOD_SHIFT) {
            SetCursor(wxNullCursor);
        }
    }
    
    void OnKillFocus()
    {
        SetCursor(wxNullCursor);
    }
    
    //! rearrange nodes to avoid overlaps of them.
    void RearrangeNodes() override
    {
        auto node_grid = kDefaultNodeSize;
        node_grid += wxSize(kNodeAlignmentSize, kNodeAlignmentSize * 2);
        
        auto const rect = GetClientRect();
        int const num_cols = rect.GetWidth() / node_grid.GetWidth();
        int const num_rows = rect.GetHeight() / node_grid.GetHeight();
        
        auto get_next_top_grid = [&, c = 0, r = 0]() mutable {
            auto pt = wxPoint(c * node_grid.GetWidth(), r * node_grid.GetHeight());
            if(c == num_cols - 1 && r == num_rows - 1) {
                // fulfilled. do nothing.
            } else {
                c = (c + 1) % num_cols;
                if(c == 0) { r = (r + 1) % num_rows; }
            }
            return pt;
        };
        
        auto get_next_bottom_grid = [&, c = num_cols - 1, r = num_rows - 1]() mutable {
            auto pt = wxPoint((num_cols - c - 1) * node_grid.GetWidth(), r * node_grid.GetHeight());
            if(c == 0 && r == 0) {
                // fulfilled. do nothing.
            } else {
                if(c == 0) {
                    r -= 1;
                    c = num_cols - 1;
                } else {
                    c -= 1;
                }
            }
            return pt;
        };
        
        auto rearrange_node = [this](auto pred, auto &grid_function) {
            for(auto &node: node_components_) {
                if(pred(node->node_->GetProcessor().get())) { node->Move(grid_function()); }
            }
        };
        
        using GP = GraphProcessor;
        rearrange_node([](Processor *proc) { return dynamic_cast<GP::AudioInput *>(proc); },
                       get_next_top_grid);
        rearrange_node([](Processor *proc) { return dynamic_cast<GP::MidiInput *>(proc); },
                       get_next_top_grid);
        rearrange_node([](Processor *proc) { return dynamic_cast<GP::AudioOutput *>(proc); },
                       get_next_bottom_grid);
        rearrange_node([](Processor *proc) { return dynamic_cast<GP::MidiOutput *>(proc); },
                       get_next_bottom_grid);        
    }
    
private:
    using NodeComponentPtr = std::unique_ptr<NodeComponent>;
    std::vector<NodeComponentPtr> node_components_;
    GraphProcessor *graph_ = nullptr;
    ScopedListenerRegister<MyApp::ChangeProjectListener> slr_change_project_;
    //ForegroundScreen *screen_;
    
    struct LineSetting {
        wxPoint begin_;
        wxPoint end_;
        wxPen pen_;
    };
    
    std::optional<LineSetting> dragging_line_;
    
    GraphicsBuffer back_buffer_;
    
    void OnPaint()
    {
        wxPaintDC pdc(this);
        wxGCDC dc(pdc);
        dc.Clear();

        back_buffer_.Clear();
        Render();
                
        wxMemoryDC memory_dc(back_buffer_.GetBitmap());

        dc.Blit(wxPoint{0, 0}, GetClientSize(), &memory_dc, wxPoint {0, 0});
    }
    
    void Render()
    {
        wxMemoryDC memory_dc(back_buffer_.GetBitmap());
        wxGCDC dc(memory_dc);
        {
            dc.SetBackground(wxBrush(kGraphBackground));
            dc.Clear();
        }
        
        DrawGrid(dc);

        for(auto &nc: node_components_) {
            nc->RenderWithParentDC(dc);
        }

        PaintOverChildren(dc); 
    }
    
    static wxPoint GetCenter(wxRect const &rect)
    {
        return wxPoint(rect.GetX() + rect.GetWidth() / 2,
                       rect.GetY() + rect.GetHeight() / 2);
    }
    
    void DrawGrid(wxDC &dc)
    {
        auto const size = GetClientSize();
        BrushPen bg(HSVToColour(0, 0, 0.9, 0.1));
        bg.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
        dc.SetPen(wxPen(HSVToColour(0.0, 0.0, 0.9, 0.2)));
        for(int x = kNodeAlignmentSize; x < size.x; x += kNodeAlignmentSize) {
            for(int y = kNodeAlignmentSize; y < size.y; y += kNodeAlignmentSize) {
                dc.DrawPoint(x, y);
            }
        }
    }
    
    void DrawConnection(wxDC &dc,
                        GraphProcessor::AudioConnection const &conn,
                        NodeComponent *nc_upstream,
                        NodeComponent *nc_downstream
                        )
    {
        auto pt_up = nc_upstream->GetPinCenter(NodeComponent::Pin::MakeAudioOutput(conn.upstream_channel_index_));
        pt_up += nc_upstream->GetPosition();
        auto pt_down = nc_downstream->GetPinCenter(NodeComponent::Pin::MakeAudioInput(conn.downstream_channel_index_));
        pt_down += nc_downstream->GetPosition();
        
        dc.SetPen(kAudioLine);
        dc.DrawLine(pt_up, pt_down);
    }
    
    void DrawConnection(wxDC &dc,
                        GraphProcessor::MidiConnection const &conn,
                        NodeComponent *nc_upstream,
                        NodeComponent *nc_downstream
                        )
    {
        auto pt_up = nc_upstream->GetPinCenter(NodeComponent::Pin::MakeMidiOutput(conn.upstream_channel_index_));
        pt_up += nc_upstream->GetPosition();
        auto pt_down = nc_downstream->GetPinCenter(NodeComponent::Pin::MakeMidiInput(conn.downstream_channel_index_));
        pt_down += nc_downstream->GetPosition();
        
        dc.SetPen(kMidiLine);
        dc.DrawLine(pt_up, pt_down);
    }
    
    void PaintOverChildren(wxDC &dc)
    {
        // draw connection;
        for(auto const &nc_upstream: node_components_) {
            std::for_each(node_components_.begin(), node_components_.end(),
                          [&](auto &nc_downstream) {
                              auto audio_conns = nc_upstream->node_->GetAudioConnectionsTo(BusDirection::kOutputSide,
                                                                                           nc_downstream->node_);
                              for(auto conn: audio_conns) { DrawConnection(dc, *conn, nc_upstream.get(), nc_downstream.get()); }
                              
                              auto midi_conns = nc_upstream->node_->GetMidiConnectionsTo(BusDirection::kOutputSide,
                                                                                           nc_downstream->node_);
                              for(auto conn: midi_conns) { DrawConnection(dc, *conn, nc_upstream.get(), nc_downstream.get()); }
                          });
        }
        
        if(dragging_line_) {
            dc.SetPen(dragging_line_->pen_);
            dc.DrawLine(dragging_line_->begin_,
                        dragging_line_->end_
                        );
        }
    }
    
    bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames)
    {
        if(filenames.size() == 1 && wxFileName(filenames[0]).GetExt() == ".trproj") {
            CallAfter([path = filenames[0]] {
                auto app = MyApp::GetInstance();
                app->LoadProject(path.ToStdWstring());
            });
            
            return true;
        }
        
        auto pred = [](auto const &name) {
            auto ext = wxFileName(name).GetExt();
            return ext == "mid" || ext == "smf";
        };
        
        bool const all_smf = std::all_of(filenames.begin(), filenames.end(), pred);
        if(all_smf) {
            CallAfter([paths = filenames] {
                auto app = MyApp::GetInstance();
                for(auto path: paths) {
                    app->ImportFile(path.ToStdWstring());
                }
            });
            
            return true;
        }
        
        return false;
    }
    
    void OnAfterNodeIsAdded(GraphProcessor::Node *node) override
    {
        auto nc = std::make_unique<NodeComponent>(this, node, this);
        nc->UseDefaultPaintMethod(false);
        node_components_.push_back(std::move(nc));
        
        // open editor automatically.
        node_components_.back()->OpenEditor();
        
        //screen_->Raise();
        Refresh();
    }
    
    void OnBeforeNodeIsRemoved(GraphProcessor::Node *node) override
    {
        auto found = std::find_if(node_components_.begin(), node_components_.end(),
                                  [node](auto const &nc) { return nc->node_ == node; });
        if(found == node_components_.end()) {
            return;
        }
        
        auto nc = std::move(*found);
        node_components_.erase(found);
        nc.reset();
        Refresh();
    }
};

std::unique_ptr<GraphEditor> CreateGraphEditorComponent(wxWindow *parent, GraphProcessor &graph)
{
    auto p = std::make_unique<GraphEditorImpl>(parent);
    p->SetGraph(graph);
    
    return p;
}

NS_HWM_END
