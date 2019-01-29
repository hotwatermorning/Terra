#include "GraphEditor.hpp"

#include <tuple>

#include "../App.hpp"
#include "./PluginEditor.hpp"
#include "../plugin/PluginScanner.hpp"
#include "./Util.hpp"
#include "../resource/ResourceHelper.hpp"

NS_HWM_BEGIN

BrushPenSet bps_audio_pin_ = {
    { HSVToColour(0.2, 0.6, 0.7), HSVToColour(0.2, 0.6, 0.8) },
    { HSVToColour(0.2, 0.6, 0.95), HSVToColour(0.2, 0.6, 1.0) },
    { HSVToColour(0.2, 0.6, 0.9), HSVToColour(0.2, 0.6, 0.95)}
};

BrushPenSet bps_midi_pin_ = {
    { HSVToColour(0.5, 0.6, 0.7), HSVToColour(0.5, 0.6, 0.8) },
    { HSVToColour(0.5, 0.6, 0.95), HSVToColour(0.5, 0.6, 1.0) },
    { HSVToColour(0.5, 0.6, 0.9), HSVToColour(0.5, 0.6, 0.95)}
};

wxPen const kAudioLine = wxPen(HSVToColour(0.2, 0.8, 1.0), 2, wxPENSTYLE_SOLID);
wxPen const kMidiLine = wxPen(HSVToColour(0.5, 0.8, 1.0), 2, wxPENSTYLE_SOLID);
wxPen const kScissorLine = wxPen(HSVToColour(0.5, 0.0, 0.7), 2, wxPENSTYLE_SHORT_DASH);

BrushPen const background = BrushPen(HSVToColour(0.0, 0.0, 0.9));
BrushPen const background_having_focus = BrushPen(HSVToColour(0.0, 0.0, 0.9), HSVToColour(0.7, 1.0, 0.9));

wxSize const kDefaultNodeSize = { 200, 60 };

class NodeComponent
:   public wxPanel
{
public:
    struct Callback {
        virtual ~Callback() {}
        virtual void OnRequestToUnload(NodeComponent *nc) = 0;
        //! ptはparent基準
        virtual void OnMouseMove(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) = 0;
        //! ptはparent基準
        virtual void OnReleaseMouse(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) = 0;
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
    NodeComponent(wxWindow *parent, GraphProcessor::Node *node, Callback *callback)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, kDefaultNodeSize)
    ,   node_(node)
    ,   callback_(callback)
    {
        btn_open_editor_ = new wxButton(this, wxID_ANY, "E", wxDefaultPosition, wxSize(20, 20));
        btn_open_editor_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnOpenEditor(); });
        if(dynamic_cast<Vst3AudioProcessor *>(node_->GetProcessor().get()) == nullptr) {
            btn_open_editor_->Hide();
        }
        
        st_plugin_name_ = new wxStaticText(this, wxID_ANY, node->GetProcessor()->GetName(),
                                           wxDefaultPosition, wxSize(1, 20), wxALIGN_CENTRE_HORIZONTAL);
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->AddStretchSpacer();
        hbox->Add(1, btn_open_editor_->GetSize().y);
        hbox->Add(btn_open_editor_, wxSizerFlags(0).Expand());
        vbox->Add(hbox, wxSizerFlags(0).Expand());
        vbox->Add(st_plugin_name_, wxSizerFlags(0).Expand());
        vbox->AddStretchSpacer();
        
        SetSizer(vbox);

        Layout();

        //! wxStaticText eats mouse down events.
        //! To pass the events to the parent, call ev.Skip().
        st_plugin_name_->Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { ev.Skip(); });
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) { OnRightUp(ev); });
        //Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](auto &ev) { OnCaptureLost(); });
        Bind(wxEVT_KILL_FOCUS, [this](auto &ev) { OnCaptureLost(); });
        Bind(wxEVT_LEAVE_WINDOW, [this](auto &ev) { OnMouseLeave(ev); });
    }
    
    ~NodeComponent()
    {
        if(editor_frame_) { editor_frame_->Destroy(); }
    }
    
    void OnOpenEditor()
    {
        if(editor_frame_) { return; }
        
        auto proc = dynamic_cast<Vst3AudioProcessor *>(node_->GetProcessor().get());
        if(!proc) { return; }
        
        editor_frame_ = CreatePluginEditorFrame(this,
                                                proc->plugin_.get(),
                                                [this] {
                                                    editor_frame_ = nullptr;
                                                    btn_open_editor_->Enable();
                                                });
        btn_open_editor_->Disable();
    }
    
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
        Draw(pdc);
    }
    
    void Draw(wxDC &dc)
    {
        auto const rect = GetClientRect();
        
        if(HasFocus()) {
            background_having_focus.ApplyTo(dc);
        } else {
            background.ApplyTo(dc);
        }
        
        auto rc_bg = rect;
        //rc_bg.Offset(0, kPinSize);
        //rc_bg.Deflate(0, kPinSize * 2);
        
        dc.DrawRectangle(rc_bg);
        
        auto const p = node_->GetProcessor().get();
        
        auto pt = ScreenToClient(::wxGetMousePosition());
        
        int const num_ai = p->GetAudioChannelCount(BusDirection::kInputSide);
        int const num_ao = p->GetAudioChannelCount(BusDirection::kOutputSide);
        int const num_mi = p->GetMidiChannelCount(BusDirection::kInputSide);
        int const num_mo = p->GetMidiChannelCount(BusDirection::kOutputSide);
        
        auto hover_pin = GetPin(pt);

        auto draw_pin = [&, this](auto pin, auto &brush_pen_set) {
            if(pin == hover_pin) {
                brush_pen_set.hover_.ApplyTo(dc);
            } else if(pin == selected_pin_) {
                brush_pen_set.selected_.ApplyTo(dc);
            } else {
                brush_pen_set.normal_.ApplyTo(dc);
            }
            
            auto center = GetPinCenter(pin);
            dc.DrawCircle(center, kPinSize);
        };
        
        for(int i = 0; i < num_ai; ++i) { draw_pin(Pin::MakeAudioInput(i), bps_audio_pin_); }
        for(int i = 0; i < num_ao; ++i) { draw_pin(Pin::MakeAudioOutput(i), bps_audio_pin_); }
        for(int i = 0; i < num_mi; ++i) { draw_pin(Pin::MakeMidiInput(i), bps_midi_pin_); }
        for(int i = 0; i < num_mo; ++i) { draw_pin(Pin::MakeMidiOutput(i), bps_midi_pin_); }
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
    
    int const kPinSize = 8;
    
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
                auto rc = wxRect(center.x - kPinSize, center.y - kPinSize,
                                 kPinSize * 2, kPinSize * 2);
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
                       (pin.dir_ == BusDirection::kInputSide ? kPinSize : rect.GetHeight() - kPinSize)
                       );
    }
    
    void MoveConstrained(wxPoint pt)
    {
        pt.x = std::max<int>(pt.x, 0);
        pt.y = std::max<int>(pt.y, 0);
        pt.x = std::min<int>(pt.x, GetParent()->GetClientSize().x - GetClientSize().x);
        pt.y = std::min<int>(pt.y, GetParent()->GetClientSize().y - GetClientSize().y);
        Move(pt);
        GetParent()->Refresh();
    }
    
    int const kID_RequestToUnload = 100;
    
    void OnRightUp(wxMouseEvent &ev)
    {
        wxMenu menu;
        menu.Append(kID_RequestToUnload, "Unload");
        menu.Bind(wxEVT_COMMAND_MENU_SELECTED, [this](auto &ev) {
            if(ev.GetId() == kID_RequestToUnload) { callback_->OnRequestToUnload(this); }
        });
        
        PopupMenu(&menu);
    }
    
    GraphProcessor::Node *node_ = nullptr;
    wxStaticText    *st_plugin_name_ = nullptr;
    wxButton        *btn_open_editor_ = nullptr;
    wxFrame         *editor_frame_ = nullptr;
    std::optional<Pin> selected_pin_;
    std::optional<wxPoint> delta_; // window 移動
    std::optional<wxPoint> pin_drag_begin_; // pin選択
    std::function<void()> request_to_unload_;
    Callback *callback_ = nullptr;
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

GraphEditor::GraphEditor(wxWindow *parent)
:   wxPanel(parent)
{}

GraphEditor::~GraphEditor()
{}

class GraphEditorImpl
:   public GraphEditor
,   public NodeComponent::Callback
{
public:
    wxCursor scissors_;
    GraphEditorImpl(wxWindow *parent)
    :   GraphEditor(parent)
    {
        wxImage img;
        
        img.LoadFile(GetResourcePath(L"/cursor/scissors.png"));
        int const cursor_x = 24;
        int const cursor_y = 24;

        img = img.Scale(cursor_x, cursor_y, wxIMAGE_QUALITY_HIGH);
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, cursor_x / 2);
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, cursor_y / 2);
        
        scissors_ = wxCursor(img);
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) { OnRightUp(ev); });
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { OnKeyDown(ev); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { OnKeyUp(ev); });
        Bind(wxEVT_KILL_FOCUS, [this](auto &ev) { OnKillFocus(); });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](auto &ev) { OnReleaseMouse(); });
    }
    
    ~GraphEditorImpl()
    {}
    
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
        static auto find_node_component = [this](auto *node) -> NodeComponent * {
            auto found = std::find_if(node_components_.begin(),
                                      node_components_.end(),
                                      [node](auto &nc) { return nc->node_ == node; });
            if(found == node_components_.end()) {
                return nullptr;
            } else {
                return found->get();
            }
        };
        
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
                auto nc2 = find_node_component(node_down);
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
                      std::tuple<int, std::string const &>(get_category_number(rhs), lhs.name());
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
    
    void AddNode(PluginDescription const &desc, wxPoint pt)
    {
        auto app = MyApp::GetInstance();
        auto proc = std::make_shared<Vst3AudioProcessor>(app->CreateVst3Plugin(desc));
        auto node = graph_->AddNode(proc);
        
        auto nc = std::make_unique<NodeComponent>(this, node.get(), this);
        nc->MoveConstrained(pt);
        node_components_.push_back(std::move(nc));
    }

    //! return true if removed.
    //! return false if not found.
    bool RemoveNode(Processor const *proc)
    {
        auto found = std::find_if(node_components_.begin(), node_components_.end(),
                                  [proc](auto const &nc) { return nc->node_->GetProcessor().get() == proc; });
        if(found == node_components_.end()) { return false; }
        
        auto node = std::move(*found);
        
        node_components_.erase(found);
        // リストから取り除く最中にデストラクタが走ると、
        // その中で親のnode_components_を参照することがあった場合に変なアクセスが発生する可能性がある。。
        // そのようなバグを避けるために、リストから取り除いたあとでデストラクタを実行する。
        node.reset();
        
        graph_->RemoveNode(proc);
        Refresh();
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
            node_components_.push_back(std::move(nc));
        }
    }
    
    void RemoveGraph()
    {
        node_components_.clear();
        graph_ = nullptr;
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
        ls.pen_ = kAudioLine;
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
    
    void OnKeyDown(wxKeyEvent const &ev)
    {
        if(ev.GetModifiers() == wxMOD_SHIFT) {
            SetCursor(scissors_);
        }
    }
    
    void OnKeyUp(wxKeyEvent const &ev)
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
        node_grid += wxSize(5, 5);
        
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
        
        auto rearrange_node = [this](auto pred, auto grid_function) {
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
    
    struct LineSetting {
        wxPoint begin_;
        wxPoint end_;
        wxPen pen_;
    };
    
    std::optional<LineSetting> dragging_line_;
    
    void OnPaint(wxPaintEvent &)
    {
        wxPaintDC pdc(this);
        Draw(pdc);
    }
    
    static wxPoint GetCenter(wxRect const &rect)
    {
        return wxPoint(rect.GetX() + rect.GetWidth() / 2,
                       rect.GetY() + rect.GetHeight() / 2);
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
    
    void Draw(wxDC &dc)
    {
        dc.SetBrush(wxBrush(wxColour(33, 33, 33)));
        dc.DrawRectangle(GetClientRect());

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
};

std::unique_ptr<GraphEditor> CreateGraphEditorComponent(wxWindow *parent, GraphProcessor &graph)
{
    auto p = std::make_unique<GraphEditorImpl>(parent);
    p->SetGraph(graph);
    
    return p;
}

NS_HWM_END
