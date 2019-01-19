#include "GraphEditor.hpp"

#include <tuple>

#include "../App.hpp"
#include "./PluginEditor.hpp"
#include "../plugin/PluginScanner.hpp"

NS_HWM_BEGIN

class NodeComponent
:   public wxPanel
{
public:
    struct Callback {
        virtual ~Callback() {}
        virtual void OnRequestToUnload(NodeComponent *nc) = 0;
        //! ptはparent基準
        virtual void OnMouseMove(NodeComponent *nc, wxPoint pt) = 0;
        //! ptはparent基準
        virtual void OnReleaseMouse(NodeComponent *nc, wxPoint pt_begin, wxPoint pt_end) = 0;
    };
    
    struct Pin
    {
        static Pin MakeInput(int index) { return Pin { BusDirection::kInputSide, index }; }
        static Pin MakeOutput(int index) { return Pin { BusDirection::kOutputSide, index }; }
        
        BusDirection dir_;
        int index_;
        
        bool operator==(Pin const &rhs) const
        {
            return dir_ == rhs.dir_ && index_ == rhs.index_;
        }
        
        bool operator!=(Pin const &rhs) const
        {
            return !(*this == rhs);
        }
    };
    
public:
    NodeComponent(wxWindow *parent, GraphProcessor::Node *node, Callback *callback)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(240, 80))
    ,   node_(node)
    ,   callback_(callback)
    {
        btn_open_editor_ = new wxButton(this, wxID_ANY, "E", wxDefaultPosition, wxSize(20, 20));
        btn_open_editor_->Bind(wxEVT_BUTTON, [this](auto &ev) { OnOpenEditor(); });
        
        st_plugin_name_ = new wxStaticText(this, wxID_ANY, node->GetProcessor()->GetName(),
                                           wxDefaultPosition, wxSize(1, 20), wxALIGN_CENTRE_HORIZONTAL);
        
        auto vbox = new wxBoxSizer(wxVERTICAL);
        auto hbox = new wxBoxSizer(wxHORIZONTAL);
        hbox->AddStretchSpacer();
        hbox->Add(btn_open_editor_, wxSizerFlags(0).Expand());
        vbox->Add(hbox, wxSizerFlags(0).Expand());
        vbox->Add(st_plugin_name_, wxSizerFlags(0).Expand());
        vbox->AddStretchSpacer();
        
        SetSizer(vbox);

        Layout();

        st_plugin_name_->Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { GetParent()->ProcessWindowEvent(ev); });
        st_plugin_name_->Bind(wxEVT_LEFT_UP, [this](auto &ev) { GetParent()->ProcessWindowEvent(ev); });
        
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
    
    wxColour HSVToColour(float hue, float saturation, float value, float opaque = 1.0)
    {
        assert(0 <= hue && hue <= 1.0);
        assert(0 <= saturation && saturation <= 1.0);
        assert(0 <= value && value <= 1.0);
        assert(0 <= opaque && opaque <= 1.0);
        
        wxImage::HSVValue hsv { hue, saturation, value };
        auto rgb = wxImage::HSVtoRGB(hsv);
        wxColour col;
        col.Set(rgb.red, rgb.green, rgb.blue, std::min<int>(std::round(opaque * 256), 255));
        return col;
    }
    
    struct BrushPen
    {
        BrushPen(wxColour col) : BrushPen(col, col) {}
        BrushPen(wxColour brush, wxColour pen)
        : brush_(wxBrush(brush))
        , pen_(wxPen(pen))
        {}
        
        wxBrush brush_;
        wxPen pen_;
        
        void ApplyTo(wxDC &dc) const {
            dc.SetBrush(brush_);
            dc.SetPen(pen_);
        }
    };
    
    BrushPen const pin_selected = BrushPen(HSVToColour(0.5, 0.9, 0.7), HSVToColour(0.5, 0.9, 0.8));
    BrushPen const pin_normal = BrushPen(HSVToColour(0.5, 0.9, 0.5), HSVToColour(0.5, 0.9, 0.6));
    BrushPen const pin_hover = BrushPen(HSVToColour(0.5, 0.9, 0.8), HSVToColour(0.5, 0.9, 0.9));
    BrushPen const background = BrushPen(HSVToColour(0.0, 0.0, 0.9));
    BrushPen const background_having_focus = BrushPen(HSVToColour(0.0, 0.0, 0.9), HSVToColour(0.7, 1.0, 0.9));
    
    void Draw(wxDC &dc)
    {
        auto const rect = GetClientRect();
        
        if(HasFocus()) {
            background_having_focus.ApplyTo(dc);
        } else {
            background.ApplyTo(dc);
        }
        
        dc.DrawRectangle(rect);
        
        auto const p = node_->GetProcessor().get();
        
        auto pt = ScreenToClient(::wxGetMousePosition());
        
        int const ninput = p->GetAudioChannelCount(BusDirection::kInputSide);
        int const noutput = p->GetAudioChannelCount(BusDirection::kOutputSide);
        
        auto hover_pin = GetPin(pt);

        auto draw_pin = [&, this](auto pin) {
            if(pin == hover_pin) {
                pin_hover.ApplyTo(dc);
            } else if(pin == selected_pin_) {
                pin_selected.ApplyTo(dc);
            } else {
                pin_normal.ApplyTo(dc);
            }
            
            auto center = GetPinCenter(pin);
            dc.DrawCircle(center, kPinSize);
        };
        
        for(int i = 0; i < ninput; ++i) { draw_pin(Pin::MakeInput(i)); }
        for(int i = 0; i < noutput; ++i) { draw_pin(Pin::MakeOutput(i)); }
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
                callback_->OnMouseMove(this, pt);
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
        Refresh();
    }
    
    int const kPinSize = 8;
    
    // ptは、ウィンドウ相対座標。
    // 見つからないときはnulloptが返る。
    std::optional<Pin> GetPin(wxPoint pt) const
    {
        auto get_impl = [this](wxPoint pt, BusDirection dir) -> std::optional<Pin> {
            int const n = node_->GetProcessor()->GetAudioChannelCount(dir);
            
            for(int i = 0; i < n; ++i) {
                Pin pin { dir, i };
                auto center = GetPinCenter(pin);
                auto rc = wxRect(center.x - kPinSize, center.y - kPinSize,
                                 kPinSize * 2, kPinSize * 2);
                if(rc.Contains(pt)) {
                    return pin;
                }
            }
            
            return std::nullopt;
        };
        
        auto pin = get_impl(pt, BusDirection::kInputSide);
        if(!pin) {
            pin = get_impl(pt, BusDirection::kOutputSide);
        }
        
        return pin;
    }
    
    wxPoint GetPinCenter(Pin pin) const
    {
        auto rect = GetClientRect();
        
        int const n = node_->GetProcessor()->GetAudioChannelCount(pin.dir_);
        int const width_audio_pins = rect.GetWidth() - 20;
        
        double const width_audio_pin = width_audio_pins / (double)n;
        return wxPoint(width_audio_pin * (pin.index_ + 0.5),
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

class GraphEditor
:   public wxPanel
,   public NodeComponent::Callback
{
public:
    GraphEditor(wxWindow *parent)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    {
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) { OnRightUp(ev); });
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
        
        auto audio_input = graph_->GetAudioInput(0);
        auto audio_output = graph_->GetAudioOutput(0);
        
//        int const in_channels = std::min<int>(node->GetProcessor()->GetAudioChannelCount(BusDirection::kInputSide),
//                                              audio_input->GetAudioChannelCount(BusDirection::kOutputSide));
//
//        for(int ch = 0; ch < in_channels; ++ch) {
//            auto device_node = graph_->GetNodeOf(audio_input);
//            graph_->ConnectAudio(device_node.get(), node.get(), ch, ch);
//        }
//
//        int const out_channels = std::min<int>(node->GetProcessor()->GetAudioChannelCount(BusDirection::kOutputSide),
//                                               audio_output->GetAudioChannelCount(BusDirection::kInputSide));
//
//        for(int ch = 0; ch < out_channels; ++ch) {
//            auto device_node = graph_->GetNodeOf(audio_output);
//            graph_->ConnectAudio(node.get(), device_node.get(), ch, ch);
//        }
    }
    
    void RemoveNode(Processor const *proc)
    {
        auto found = std::find_if(node_components_.begin(), node_components_.end(),
                                  [proc](auto const &nc) { return nc->node_->GetProcessor().get() == proc; });
        assert(found != node_components_.end());
        
        auto node = std::move(*found);
        
        node_components_.erase(found);
        graph_->RemoveNode(proc);
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
        RemoveNode(nc->node_->GetProcessor().get());
    }
    
    //! ptはparent基準
    void OnMouseMove(NodeComponent *nc, wxPoint pt) override
    {
        
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
            
            graph_->ConnectAudio(nup, ndown, chup, chdown);
            Refresh();
            break;
        }
    }
    
private:
    using NodeComponentPtr = std::unique_ptr<NodeComponent>;
    std::vector<NodeComponentPtr> node_components_;
    GraphProcessor *graph_ = nullptr;
    
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
        auto pt_up = nc_upstream->GetPinCenter(NodeComponent::Pin::MakeOutput(conn.upstream_channel_index_));
        pt_up += nc_upstream->GetPosition();
        auto pt_down = nc_downstream->GetPinCenter(NodeComponent::Pin::MakeInput(conn.downstream_channel_index_));
        pt_down += nc_downstream->GetPosition();
        
        dc.SetPen(wxPen(wxColour(0xDD, 0xDD, 0x35, 0xCC), 2, wxPENSTYLE_SOLID));
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
                              auto conns = nc_upstream->node_->GetAudioConnectionsTo(BusDirection::kOutputSide, nc_downstream->node_);
                              for(auto conn: conns) { DrawConnection(dc, *conn, nc_upstream.get(), nc_downstream.get()); }
                          });
        }
    }
};

std::unique_ptr<wxPanel> CreateGraphEditorComponent(wxWindow *parent, GraphProcessor &graph)
{
    auto p = std::make_unique<GraphEditor>(parent);
    p->SetGraph(graph);
    
    return p;
}

NS_HWM_END
