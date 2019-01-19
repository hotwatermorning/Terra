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
    NodeComponent(wxWindow *parent, GraphProcessor::Node *node, std::function<void()> request_to_unload)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(240, 80))
    ,   node_(node)
    ,   request_to_unload_(request_to_unload)
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
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_RIGHT_UP, [this](auto &ev) { OnRightUp(ev); });
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
    
    void Draw(wxDC &dc)
    {
        auto const rect = GetClientRect();
        
        background.ApplyTo(dc);
        dc.DrawRectangle(rect);
        
        auto const p = node_->GetProcessor().get();
        
        auto pt = ScreenToClient(::wxGetMousePosition());
        
        int const ninput = p->GetAudioChannelCount(BusDirection::kInputSide);
        auto const hover_input_pin_index = GetPinIndex(BusDirection::kInputSide, pt);
        
        for(int i = 0; i < ninput; ++i) {
            auto center = GetPinCenter(BusDirection::kInputSide, i);
            
            if(i == hover_input_pin_index) {
                pin_hover.ApplyTo(dc);
            } else if(i == selected_input_pin_) {
                pin_selected.ApplyTo(dc);
            } else {
                pin_normal.ApplyTo(dc);
            }
            
            dc.DrawCircle(center, kPinSize);
        }
        
        int const noutput = node_->GetProcessor()->GetAudioChannelCount(BusDirection::kOutputSide);
        auto const hover_output_pin_index = GetPinIndex(BusDirection::kOutputSide, pt);
        
        for(int i = 0; i < noutput; ++i) {
            auto center = GetPinCenter(BusDirection::kOutputSide, i);
            
            if(i == hover_output_pin_index) {
                pin_hover.ApplyTo(dc);
            } else if(i == selected_output_pin_) {
                pin_selected.ApplyTo(dc);
            } else {
                pin_normal.ApplyTo(dc);
            }
            
            dc.DrawCircle(center, kPinSize);
        }
    }
    
    void OnLeftDown(wxMouseEvent& ev)
    {
        selected_input_pin_ = GetPinIndex(BusDirection::kInputSide, ev.GetPosition());
        selected_output_pin_ = GetPinIndex(BusDirection::kOutputSide, ev.GetPosition());
        
        if(selected_input_pin_ != -1 || selected_output_pin_ != -1) {
            Refresh();
            return;
        }
        
        CaptureMouse();
        Raise();
        wxPoint pos = ::wxGetMousePosition();
        wxPoint origin = this->GetPosition();
        delta_ = pos - origin;
    }
    
    void OnLeftUp(wxMouseEvent& ev)
    {
        if (HasCapture())
            ReleaseMouse();
    }
    
    void OnMouseMove(wxMouseEvent& ev)
    {
        if(!HasCapture()) {
            Refresh();
            return;
        }
        
        if (ev.Dragging() && ev.LeftIsDown())
        {
            wxPoint pos = ::wxGetMousePosition();
            MoveConstrained(pos - delta_);
        }
    }
    
    int const kPinSize = 8;
    
    // ptは、ウィンドウ相対座標。
    // 見つからないときは-1が返る。
    int GetPinIndex(BusDirection dir, wxPoint pt)
    {
        int const n = node_->GetProcessor()->GetAudioChannelCount(dir);
        
        for(int i = 0; i < n; ++i) {
            auto center = GetPinCenter(dir, i);
            auto rc = wxRect(center.x - kPinSize, center.y - kPinSize,
                             kPinSize * 2, kPinSize * 2);
            if(rc.Contains(pt)) {
                return i;
            }
        }
        
        return -1;
    }
    
    wxPoint GetPinCenter(BusDirection dir, int pin_index)
    {
        auto rect = GetClientRect();
        
        int const n = node_->GetProcessor()->GetAudioChannelCount(dir);
        int const width_audio_pins = rect.GetWidth() - 20;
        
        double const width_audio_pin = width_audio_pins / (double)n;
        return wxPoint(width_audio_pin * (pin_index + 0.5),
                       (dir == BusDirection::kInputSide ? kPinSize : rect.GetHeight() - kPinSize)
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
            if(ev.GetId() == kID_RequestToUnload) { request_to_unload_(); }
        });
        
        PopupMenu(&menu);
    }

    GraphProcessor::Node *node_ = nullptr;
    wxStaticText    *st_plugin_name_ = nullptr;
    wxButton        *btn_open_editor_ = nullptr;
    wxFrame         *editor_frame_ = nullptr;
    wxPoint delta_;
    std::function<void()> request_to_unload_;
    int selected_input_pin_ = -1;
    int selected_output_pin_ = -1;
};

class GraphEditor
:   public wxPanel
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
        
        auto request_to_unload = [this, p = proc.get()]() { RemoveNode(p); };
        auto nc = std::make_unique<NodeComponent>(this, node.get(), request_to_unload);
        nc->MoveConstrained(pt);
        node_components_.push_back(std::move(nc));
        
        auto audio_input = graph_->GetAudioInput(0);
        auto audio_output = graph_->GetAudioOutput(0);
        
        int const in_channels = std::min<int>(node->GetProcessor()->GetAudioChannelCount(BusDirection::kInputSide),
                                              audio_input->GetAudioChannelCount(BusDirection::kOutputSide));
        
        for(int ch = 0; ch < in_channels; ++ch) {
            auto device_node = graph_->GetNodeOf(audio_input);
            graph_->ConnectAudio(device_node.get(), node.get(), ch, ch);
        }
        
        int const out_channels = std::min<int>(node->GetProcessor()->GetAudioChannelCount(BusDirection::kOutputSide),
                                               audio_output->GetAudioChannelCount(BusDirection::kInputSide));
        
        for(int ch = 0; ch < out_channels; ++ch) {
            auto device_node = graph_->GetNodeOf(audio_output);
            graph_->ConnectAudio(node.get(), device_node.get(), ch, ch);
        }
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
            auto request_to_unload = [this, p = node->GetProcessor().get()]() { RemoveNode(p); };
            auto nc = std::make_unique<NodeComponent>(this, node.get(), request_to_unload);
            node_components_.push_back(std::move(nc));
        }
    }
    
    void RemoveGraph()
    {
        node_components_.clear();
        graph_ = nullptr;
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
    
    void Draw(wxDC &dc)
    {
        dc.SetBrush(wxBrush(wxColour(33, 33, 33)));
        dc.DrawRectangle(GetClientRect());

        // draw connection;
        for(auto const &nc: node_components_) {
            auto node = nc->node_;
            std::for_each(node_components_.begin(),
                          node_components_.end(),
                          [&](auto &nc2) {
                              auto conns = node->GetAudioConnectionsTo(BusDirection::kOutputSide, nc2->node_);
                              if(conns.empty()) { return; }
                              
                              auto pt_upstream = GetCenter(nc->GetRect());
                              auto pt_downstream = GetCenter(nc2->GetRect());
                              
                              dc.SetPen(wxPen(wxColour(0xDD, 0xDD, 0x35, 0xCC), 2, wxPENSTYLE_SOLID));
                              dc.DrawLine(pt_upstream, pt_downstream);
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
