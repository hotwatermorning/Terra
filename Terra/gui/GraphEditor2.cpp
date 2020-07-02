#include "GraphEditor2.hpp"

#include "../App.hpp"
#include "./PluginEditor.hpp"
#include "./DataType.hpp"
#include "./Button.hpp"
#include "./Util.hpp"
#include "../misc/ScopeExit.hpp"
#include "../misc/UndoManager.hpp"

NS_HWM_BEGIN

class NodeComponent2;

struct Node
{
    Node(String name, int num_inputs, int num_outputs)
    :   name_(name)
    ,   num_inputs_(num_inputs)
    ,   num_outputs_(num_outputs)
    {
        hwm::dout << "Node (" << to_utf8(name_) << ") is created." << std::endl;
    }

    Node(Node const &rhs) = delete;
    Node & operator=(Node const &rhs) = delete;
    Node(Node &&rhs) = delete;
    Node & operator=(Node &&rhs) = delete;

    ~Node()
    {
        hwm::dout << "Node (" << to_utf8(name_) << ") is deleted." << std::endl;
    }

    String const & GetName() const { return name_; }
    void SetName(String const &name) { name_ = name; }

    int GetNumInputs() const noexcept { return num_inputs_; }
    int GetNumOutputs() const noexcept { return num_outputs_; }

    void SetNumInputs(int num) noexcept { num_inputs_ = num; }
    void SetNumOutputs(int num) noexcept { num_outputs_ = num; }

private:
    String name_;
    int num_inputs_ = -1;
    int num_outputs_ = -1;
};

class INodeOwner
{
public:
    virtual ~INodeOwner() {}

    virtual
    void TryToConnect(NodeComponent2 *node, bool is_input_pin, int pin_index) = 0;

    virtual
    void DeleteNode(NodeComponent2 *node) = 0;

    virtual
    bool ShowNodePopup(wxMenu &menu) = 0;
};

class NodeComponent2
:   public wxObject
{
    constexpr static int kShadowRadius = 3;
    constexpr static float kPinRadius = 4;

public:
    NodeComponent2(INodeOwner *owner, Node *node,
                   FPoint pos = {-1, -1},
                   FSize size = {-1, -1})
    :   owner_(owner)
    ,   node_(node)
    ,   rc_()
    {
        assert(node_);

        theme_ = Button::getDefaultButtonTheme();

        SetPosition(pos);
        SetSize(size);
    }

    void SetInputs(int num)
    {
        node_->SetNumInputs(num);
    }

    void SetOutput(int num)
    {
        node_->SetNumOutputs(num);
    }

    int GetNumInputs() const { return node_->GetNumInputs(); }
    int GetNumOutputs() const { return node_->GetNumOutputs(); }

    FPoint GetPinPosition(bool is_input, int index) const
    {
        FPoint pin_center;
        auto const rc = GetClientRect();
        auto const pin_count = is_input ? GetNumInputs() : GetNumOutputs();
        float const pin_x = rc.GetWidth() * (index + 1) / (pin_count + 1.0);
        pin_center.x = pin_x;
        if(is_input) {
            pin_center.y = kPinRadius + 1; // +1 は pin の枠の分
        } else {
            pin_center.y = rc.GetHeight() - kPinRadius - 2;
        }

        return pin_center;
    }

    int GetPinIndex(bool is_input, FPoint pos) const
    {
        auto contains = [](FPoint pin_center, float radius, FPoint pos) {
            auto dx = (pos.x - pin_center.x);
            auto dy = (pos.y - pin_center.y);

            return sqrt(dx * dx + dy * dy) <= (radius + 1); // pin の枠も pin の範囲内として扱う（pin のサイズが小さいのでそのほうが便利なため）
        };

        auto const pin_count = is_input ? GetNumInputs() : GetNumOutputs();
        for(int i = 0, end = pin_count; i < end; ++i) {
            auto center = GetPinPosition(is_input, i);
            if(contains(center, kPinRadius, pos)) {
                return i;
            }
        }

        return -1;
    }

    void OnPaint(wxDC &dc)
    {
        auto const rc = GetClientRect();

        Button::ButtonTheme::ColourSetting *c = &theme_.normal_;
        if(being_pushed_) {
            c = &theme_.down_;
        } else if(hover_) {
            c = &theme_.highlighted_;
        }

        c->sa_front_ = 0.5f;

        auto const round = theme_.sz_round_;
        auto const edge = theme_.sz_edge_;

        // draw shadow
        {
            ScopedTranslateDC t(dc, FSize { (float)kShadowRadius * 2.0, (float)kShadowRadius * 2.0});

            wxMemoryDC mdc;
            mdc.SelectObjectAsSource(wxBitmap(img_shadow_));
            auto shadow_image_size = wxSize {
                (int)(rc.GetWidth() + kShadowRadius * 2),
                (int)(rc.GetHeight() + kShadowRadius * 2)
            };

            dc.StretchBlit(wxPoint {kShadowRadius, kShadowRadius}, shadow_image_size, &mdc, wxPoint {0, 0}, img_shadow_.GetSize());
            //dc.DrawBitmap(img_shadow_, 0, 0);
        }

        BrushPen bp = BrushPen { HSVToColour(hue_, c->sa_front_, c->br_front_edge_) };
        bp.ApplyTo(dc);
        dc.DrawRoundedRectangle(edge, edge, rc.GetWidth() - (edge * 2), rc.GetHeight() - (edge * 3), round);

        auto const col_top = HSVToColour(hue_, c->sa_front_, c->br_front_grad_top_);
        auto const col_bottom = HSVToColour(hue_, c->sa_front_, c->br_front_grad_bottom_);

        wxGraphicsContext *gc = wxGraphicsContext::CreateFromUnknownDC(dc);
        HWM_SCOPE_EXIT([&] { delete gc; });
        assert(gc);

        if(!gc) { return; }

        auto brush = gc->CreateLinearGradientBrush(0, 0, 0, rc.GetHeight(), col_top, col_bottom);
        gc->SetBrush(brush);
        gc->DrawRoundedRectangle(edge, edge * 2, rc.GetWidth() - (edge * 2), rc.GetHeight() - (edge * 4), round);

        auto col_text_highlight = HSVToColour(hue_, 0.0, c->br_text_edge_);
        dc.SetTextForeground(col_text_highlight);
        auto rc_text_highlight = rc;
        rc_text_highlight.Deflate(0, 1);
        rc_text_highlight.Translate(0, -1);
        dc.DrawLabel(GetLabel(), rc_text_highlight, wxALIGN_CENTER);

        auto col_text = HSVToColour(hue_, 0.0, c->br_text_);
        dc.SetTextForeground(col_text);
        auto rc_text = rc;
        dc.DrawLabel(GetLabel(), rc_text, wxALIGN_CENTER);

        // draw input pins
        for(int i = 0, end = GetNumInputs(); i < end; ++i) {
            float const pin_x = rc.GetWidth() * (i+1) / (end + 1.0);
            brush = gc->CreateBrush(HSVToColour(0.1, 0.0, 0.0));
            gc->SetBrush(brush);
            gc->DrawEllipse(pin_x - (kPinRadius + 1), 0, (kPinRadius + 1) * 2, (kPinRadius + 1) * 2);

            brush = gc->CreateBrush(HSVToColour(0.1, c->sa_front_, 0.88f));
            gc->SetBrush(brush);
            gc->DrawEllipse(pin_x - kPinRadius, 1, kPinRadius * 2, kPinRadius * 2);

            auto const col_pin_top = HSVToColour(0.1, c->sa_front_, 0.76f);
            auto const col_pin_bottom = HSVToColour(0.1, c->sa_front_, 0.56f);
            brush = gc->CreateLinearGradientBrush(0, 1, 0, kPinRadius * 2 - 1, col_pin_top, col_pin_bottom);
            gc->SetBrush(brush);
            gc->DrawEllipse(pin_x - kPinRadius, 2, kPinRadius * 2, kPinRadius * 2 - 1);
        }

        // draw output pins
        for(int i = 0, end = GetNumOutputs(); i < end; ++i) {
            float const pin_x = rc.GetWidth() * (i+1) / (end + 1.0);
            brush = gc->CreateBrush(HSVToColour(0.1, 0.0, 0.0));
            gc->SetBrush(brush);
            gc->DrawEllipse(pin_x - (kPinRadius + 1), rc.GetHeight() - ((kPinRadius + 1) * 2) - 1, (kPinRadius + 1) * 2, (kPinRadius + 1) * 2);

            // pin のハイライトを描画
            brush = gc->CreateBrush(HSVToColour(0.1, c->sa_front_, 0.88f));
            gc->SetBrush(brush);
            gc->DrawEllipse(pin_x - kPinRadius, rc.GetHeight() - (kPinRadius * 2) - 2, kPinRadius * 2, kPinRadius * 2);

            // pin の本体を描画
            auto const col_pin_top = HSVToColour(0.1, c->sa_front_, 0.76f);
            auto const col_pin_bottom = HSVToColour(0.1, c->sa_front_, 0.56f);
            brush = gc->CreateLinearGradientBrush(0, rc.GetHeight() - (kPinRadius * 2) - 1,
                                                  0, rc.GetHeight() - 1,
                                                  col_pin_top, col_pin_bottom);
            gc->SetBrush(brush);
            gc->DrawEllipse(pin_x - kPinRadius, rc.GetHeight() - (kPinRadius * 2) - 1, kPinRadius * 2, kPinRadius * 2 - 1);
        }
    }

    wxString GetLabel() const { return node_->GetName(); }

    FRect GetClientRect() const noexcept { return rc_.WithPosition(0, 0); }
    FRect GetRect() const noexcept { return rc_; }
    FPoint GetPosition() const noexcept { return rc_.pos; }
    FSize GetSize() const noexcept { return rc_.size; }

    void SetPosition(FPoint pos) { rc_.pos = pos; }
    void SetSize(FSize size) {
        rc_.size = size;
        UpdateShadowBuffer();
    }

    void SetRect(FRect rc) {
        rc_ = rc;
        UpdateShadowBuffer();
    }

    struct MouseEvent
    {
        MouseEvent()
        {}

        MouseEvent(FPoint pt, wxKeyboardState key_state)
        :   pt_(pt)
        ,   key_state_(key_state)
        {}

        MouseEvent(FPoint pt, wxKeyboardState key_state, int wheel_delta, int wheel_rotation)
        :   pt_(pt)
        ,   key_state_(key_state)
        ,   wheel_delta_(wheel_delta)
        ,   wheel_rotation_(wheel_rotation)
        {}

        FPoint pt_;
        int wheel_delta_ = 0;
        int wheel_rotation_ = 0;
        wxKeyboardState key_state_;
    };

    void OnLeftDown(MouseEvent const &ev)
    {
        {
            auto const pi = GetPinIndex(true, ev.pt_);
            if(pi != -1) {
                owner_->TryToConnect(this, true, pi);
                return;
            }

        }

        {
            auto const pi = GetPinIndex(false, ev.pt_);
            if(pi != -1) {
                owner_->TryToConnect(this, false, pi);
                return;
            }

        }

        being_pushed_ = true;
    }

    void OnLeftUp(MouseEvent const &ev)
    {
        being_pushed_ = false;
    }

    void OnRightDown(MouseEvent const &ev)
    {

    }

    void OnRightUp(MouseEvent const &ev)
    {
        wxMenu menu;
        constexpr int kDeleteNode = 1000;

        menu.Append(kDeleteNode, "&Delete This Node", "Delete this node.", false);

        menu.Bind(wxEVT_MENU, [this](wxCommandEvent const &ev) {
            switch(ev.GetId()) {
                case kDeleteNode:
                    owner_->DeleteNode(this); // Undo が実装されるまでは、ここで即座に NodeComponent が削除される。
                    break;
                default:
                    assert(false && "not implemented yet.");
            }
        });

        // ここはもう少し設計をなんとかしたい。
        // Node が PopupMenu を表示するのに 明示的に NodeOwner に依頼する必要があるのは不自然。
        // コード上は自分で直接 PopupMenu を表示できるようにしたい。
        owner_->ShowNodePopup(menu);
    }

    void OnMouseMove(MouseEvent const &ev)
    {
        hover_ = (GetClientRect().Contain(ev.pt_));
    }

    void OnMouseEnter(MouseEvent const &ev)
    {
        hover_ = true;
    }

    void OnMouseLeave(MouseEvent const &evy)
    {
        hover_ = false;
    }

    void OnMouseWheel(MouseEvent const &ev)
    {

    }

    Node * GetNode() const noexcept { return node_; }

    void OnMouseEventFromParent(MouseEvent const &ev, void(NodeComponent2::* mem_fun)(MouseEvent const &ev))
    {
        auto tmp = ev;
        tmp.pt_ = ParentToChild(tmp.pt_);
        (this->*mem_fun)(tmp);
    }

    FPoint ChildToParent(FPoint pt) const
    {
        auto node_pos = GetPosition();
        pt.x += node_pos.x;
        pt.y += node_pos.y;

        return pt;
    }

    FPoint ParentToChild(FPoint pt) const
    {
        auto node_pos = GetPosition();
        pt.x -= node_pos.x;
        pt.y -= node_pos.y;

        return pt;
    }

private:
    double hue_ = 0.3;
    INodeOwner *owner_ = nullptr;
    Node *node_ = nullptr;
    FRect rc_; // 親ウィンドウ上での位置
    FRect rc_shadow_;
    Button::ButtonTheme theme_;
    //GraphicsBuffer gb_shadow_;
    wxImage img_shadow_;

    bool being_pushed_ = false;
    bool pushed_ = false;
    bool hover_ = false;

    void UpdateShadowBuffer()
    {
        // draw shadow
        {
            int round = theme_.sz_round_;
            auto rc = GetClientRect();

            auto gb_shadow = GraphicsBuffer(wxSize {
                (int)((rc.GetWidth() + kShadowRadius * 2) * 2),
                (int)((rc.GetHeight() + kShadowRadius * 2) * 2)
            });
            wxMemoryDC memory_dc(gb_shadow.GetBitmap());
            wxGCDC gcdc(memory_dc);
            gcdc.SetBackground(wxBrush(wxTransparentColour));
            gcdc.Clear();

            BrushPen bp { HSVToColour(0.0, 0.0, 0.0, 1.0) };
            bp.ApplyTo(gcdc);
            auto rc_shadow = FRect {
                FPoint { kShadowRadius * 2, kShadowRadius * 2 },
                FSize { rc.GetWidth() * 2, rc.GetHeight() * 2 }
            };

            gcdc.DrawRoundedRectangle(rc_shadow, round * 2);

            memory_dc.SelectObject(wxNullBitmap);

            img_shadow_ = gb_shadow.GetBitmap().ConvertToImage();
            img_shadow_ = img_shadow_.Blur(kShadowRadius * 2);
        }
    }
};

// ノードの追加／削除
// 可視領域（ViewWindow）の拡大縮小／移動
//     全範囲を表すデータが必要

template<class... Args>
IGraphEditor::IGraphEditor(Args&&... args)
:   wxWindow(std::forward<Args>(args)...)
{}

IGraphEditor::~IGraphEditor()
{}

class GraphEditor2
:   public IGraphEditor
//,   public NodeComponent::Callback
//,   public App::ChangeProjectListener
,   public GraphProcessor::Listener
,   public INodeOwner
{
public:
    using NodeComponent2Ptr = std::unique_ptr<NodeComponent2>;
    using NodePtr = std::unique_ptr<Node>;

    static
    FSize GetDefaultNodeSize() { return FSize { 180, 60 }; }

    struct NodeConnectionInfo
    {
        Node *upstream_ = nullptr;
        Node *downstream_ = nullptr;

        int output_pin_ = -1; // upstream から出力するピンの番号
        int input_pin_ = -1;  // downtream から出力するピンの番号

        bool selected_ = false;

        //! compare NodeConnectionInfo and return true if equals.
        /*! @note selected_ member does not perticipate in this comparison.
         */
        bool operator==(NodeConnectionInfo const &rhs) const
        {
            auto const to_tuple = [](NodeConnectionInfo const &x) {
                return std::tie(x.upstream_, x.downstream_, x.output_pin_, x.input_pin_);
            };

            return to_tuple(*this) == to_tuple(rhs);
        }

        bool operator!=(NodeConnectionInfo const &rhs) const { return !(*this == rhs); }
    };

    struct TryToConnectInfo
    {
        NodeComponent2 *from_ = nullptr;
        bool is_input_pin_ = false;
        int pin_index_ = -1;
    };

    struct NodeContainer
    {
        std::vector<NodePtr> list_;

        struct Pred {
            explicit
            Pred(Node const *p) : p_(p) {}
            bool operator()(NodePtr const &p) const { return p.get() == p_; }
        private:
            Node const *p_ = nullptr;
        };

        Node * AddNode(NodePtr p)
        {
            assert(p);
            assert(std::none_of(list_.begin(), list_.end(), Pred(p.get())));

            list_.push_back(std::move(p));
            return list_.back().get();
        }

        NodePtr RemoveNode(Node const *p)
        {
            assert(p);

            auto found = std::find_if(list_.begin(), list_.end(), Pred(p));
            if(found == list_.end()) {
                return nullptr;
            }

            auto ret = std::move(*found);
            list_.erase(found);

            return ret;
        }
    };

    GraphEditor2(wxWindow *parent, wxWindowID id)
    :   IGraphEditor(parent, id)
    {
        node_container_ = std::make_unique<NodeContainer>();

        Bind(wxEVT_PAINT, [this](wxPaintEvent &ev) { OnPaint(); });
        Bind(wxEVT_KEY_UP, [this](wxKeyEvent &ev) { OnKeyUp(ev); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](wxMouseEvent &ev) { OnMouseMove(ev); });
        Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent &ev) { OnRightUp(ev); });
        Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &ev) { OnMouseWheel(ev); });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &ev) { OnCaptureLost(ev); });

        //Bind(wxEVT_SIZING, [this](wxSizeEvent &ev) { ResizeGraphicsBuffer(ev.GetSize()); });
        Bind(wxEVT_SIZE, [this](wxSizeEvent &ev) { ResizeGraphicsBuffer(ev.GetSize()); });

        ResizeGraphicsBuffer(GetClientSize());
    }

    void ResizeGraphicsBuffer(wxSize size)
    {
         gb_ = GraphicsBuffer(size);
    }

    void OnSize(wxSizeEvent &ev)
    {

    }

    void OnPaint()
    {
#if defined(_MSC_VER)
        wxMemoryDC dc(gb_.GetBitmap());
#else
        wxPaintDC dc(this);
#endif

        BrushPen bp_background { HSVToColour(0.5, 0.2, 0.3) };
        bp_background.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());

        ScopedScaleDC sdc(dc, zoom_factor_);

        // 現在の zoom_factor_ で画面中央の view_origin がどこになるか

        auto const size = FSize(GetClientSize());
        auto const view_zoom_center = FPoint {
            (float)(-size.w * 0.5 / zoom_factor_),
            (float)(-size.h * 0.5 / zoom_factor_)
        };

        auto const view_origin_shift = FSize {
            (float)(view_origin_.x ),
            (float)(view_origin_.y)
        };

        auto view_shift = view_zoom_center - view_origin_shift;

        ScopedTranslateDC tdc(dc, FSize { view_shift.x, view_shift.y });

        // 描画範囲を検証するための基準点
//        dc.SetBrush(HSVToColour(0.0, 1.0, 1.0));
//        dc.DrawEllipse(0, 0, 10, 10);
//
//        dc.SetBrush(HSVToColour(0.33, 1.0, 1.0));
//        dc.DrawEllipse(100, 100, 10, 10);
//
//        dc.SetBrush(HSVToColour(0.66, 1.0, 1.0));
//        dc.DrawEllipse(200, 200, 10, 10);
//
//        dc.SetBrush(HSVToColour(0.0, 0, 1.0));
//        dc.DrawEllipse(300, 100, 10, 10);

        auto gc = wxGraphicsContext::CreateFromUnknownDC(dc);
        HWM_SCOPE_EXIT([gc] { delete gc; });

        auto draw_connection = [&, this](NodeConnectionInfo const &conn) {
            auto un = GetNodeComponentByPointer(conn.upstream_);
            auto dn = GetNodeComponentByPointer(conn.downstream_);

            wxPoint2DDouble pt_b {
                un->GetPosition().x + un->GetSize().w * (conn.output_pin_ + 1) / (un->GetNumOutputs() + 1.0),
                un->GetPosition().y + un->GetSize().h
            };

            wxPoint2DDouble pt_e {
                dn->GetPosition().x + dn->GetSize().w * (conn.input_pin_ + 1) / (dn->GetNumInputs() + 1.0),
                dn->GetPosition().y
            };

            wxPoint2DDouble pt1 { pt_b.m_x, (pt_b.m_y + pt_e.m_y) / 2.0 };
            wxPoint2DDouble pt2 { pt_e.m_x, (pt_b.m_y + pt_e.m_y) / 2.0 };

            auto path = gc->CreatePath();
            path.MoveToPoint(pt_b);
            path.AddCurveToPoint(pt1, pt2, pt_e);

            if(conn.selected_) {
                gc->SetPen(wxPen(HSVToColour(0.3, 0.9, 0.9, 0.9), 4.0));
            } else {
                gc->SetPen(wxPen(HSVToColour(0.3, 0.7, 0.8, 0.7), 2.0));
            }
            gc->DrawPath(path);
        };

        for(int i = 0, end = nodes_.size(); i < end; ++i) {
            auto *nc = nodes_[end - i - 1].get();
            auto const node_shift = nc->GetPosition();

            ScopedTranslateDC tdc(dc, FSize { -node_shift.x, -node_shift.y });

            nc->OnPaint(dc);

            tdc.reset();

            auto conns = GetUpstreamSideConnections(nc->GetNode());

            for(auto const &conn: conns) {
                draw_connection(conn);
            }
        }

        if(cut_mode_) {
            auto pen = wxPen(HSVToColour(0.7, 0.2, 0.95, 0.6), 2, wxPENSTYLE_DOT);
            gc->SetPen(pen);
            gc->StrokeLine(saved_logical_mouse_down_pos_.x, saved_logical_mouse_down_pos_.y,
                           latest_logical_mouse_pos_.x, latest_logical_mouse_pos_.y);
        }

        if(try_conn_) {
            auto node = try_conn_->from_;
            auto pin_pos = node->GetPinPosition(try_conn_->is_input_pin_, try_conn_->pin_index_);

            wxPoint2DDouble pt_b {
                node->GetPosition().x + pin_pos.x,
                node->GetPosition().y + pin_pos.y
            };

            wxPoint2DDouble pt_e {
                latest_logical_mouse_pos_.x, latest_logical_mouse_pos_.y
            };

            wxPoint2DDouble pt1 { pt_b.m_x, (pt_b.m_y + pt_e.m_y) / 2.0 };
            wxPoint2DDouble pt2 { pt_e.m_x, (pt_b.m_y + pt_e.m_y) / 2.0 };

            auto path = gc->CreatePath();
            path.MoveToPoint(pt_b);
            path.AddCurveToPoint(pt1, pt2, pt_e);

            gc->SetPen(wxPen(HSVToColour(0.3, 0.9, 0.9, 0.9), 2.0, wxPENSTYLE_DOT));
            gc->DrawPath(path);
        }

        tdc.reset();
        sdc.reset();

#if defined(_MSC_VER)
        wxPaintDC pdc(this);
        pdc.Blit(wxPoint{0, 0}, GetClientSize(), &dc, wxPoint(0, 0));
#endif
    }

    //! 論理座標上の位置に対応する GraphEditor クライアント領域上の位置を返す。
    FPoint LogicalToClient(FPoint pt, FPoint view_origin)
    {
        assert(zoom_factor_ > 0);

        auto const half = FSize {
            (float)GetClientSize().GetWidth(),
            (float)GetClientSize().GetHeight(),
        }.Scaled(0.5);

        auto client_pt_from_center = FPoint {
            float((pt.x + view_origin.x) * zoom_factor_),
            float((pt.y + view_origin.y) * zoom_factor_)
        };

        return client_pt_from_center + half;
    }

    FPoint LogicalToClient(FPoint pt) { return LogicalToClient(pt, view_origin_); }

    //! GraphEditor クライアント領域上の位置に対応する論理座標上の位置を返す。
    FPoint ClientToLogical(FPoint pt, FPoint view_origin)
    {
        assert(zoom_factor_ > 0);

        auto const half = FSize {
            (float)GetClientSize().GetWidth(),
            (float)GetClientSize().GetHeight(),
        }.Scaled(0.5);

        // 画面中央位置から見た pt の位置
        auto const client_pt_from_center = pt - half;

        return FPoint {
            float(client_pt_from_center.x / zoom_factor_) - view_origin.x,
            float(client_pt_from_center.y / zoom_factor_) - view_origin.y
        };
    }

    FPoint ClientToLogical(FPoint pt) { return ClientToLogical(pt, view_origin_); }

    FRect LogicalToClient(FRect rc, FPoint view_origin)
    {
        return {
            LogicalToClient(rc.GetTopLeft(), view_origin),
            LogicalToClient(rc.GetBottomRight(), view_origin)
        };
    }

    FRect ClientToLogical(FRect rc, FPoint view_origin)
    {
        return {
            ClientToLogical(rc.GetTopLeft(), view_origin),
            ClientToLogical(rc.GetBottomRight(), view_origin)
        };
    }

    FRect LogicalToClient(FRect rc) { return LogicalToClient(rc, view_origin_); }
    FRect ClientToLogical(FRect rc) { return ClientToLogical(rc, view_origin_); }

    void BringToFront(NodeComponent2 *node)
    {
        auto it = std::find_if(nodes_.begin(),
                               nodes_.end(),
                               [node](auto &n) { return n.get() == node; });
        assert(it != nodes_.end());

        auto index = it - nodes_.begin();
        if(index != 0) {
            index = 0;

            auto tmp = std::move(*it);
            nodes_.erase(it);
            nodes_.insert(nodes_.begin() + index, std::move(tmp));
        }
    }

    void BringForward(NodeComponent2 *node)
    {
        auto it = std::find_if(nodes_.begin(),
                               nodes_.end(),
                               [node](auto &n) { return n.get() == node; });
        assert(it != nodes_.end());

        auto tmp = std::move(*it);

        auto index = it - nodes_.begin();
        if(index != 0) {
            index -= 1;

            auto tmp = std::move(*it);
            nodes_.erase(it);
            nodes_.insert(nodes_.begin() + index, std::move(tmp));
        }
    }

    void SendBackward(NodeComponent2 *node)
    {
        auto it = std::find_if(nodes_.begin(),
                               nodes_.end(),
                               [node](auto &n) { return n.get() == node; });
        assert(it != nodes_.end());

        auto tmp = std::move(*it);

        auto index = it - nodes_.begin();
        if(index != nodes_.size() - 1) {
            index += 1;

            auto tmp = std::move(*it);
            nodes_.erase(it);
            nodes_.insert(nodes_.begin() + index, std::move(tmp));
        }
    }

    void SendToBack(NodeComponent2 *node)
    {
        auto it = std::find_if(nodes_.begin(),
                               nodes_.end(),
                               [node](auto &n) { return n.get() == node; });
        assert(it != nodes_.end());

        auto tmp = std::move(*it);

        auto index = it - nodes_.begin();
        if(index != nodes_.size() - 1) {
            index = nodes_.size();

            auto tmp = std::move(*it);
            nodes_.erase(it);
            nodes_.insert(nodes_.begin() + index, std::move(tmp));
        }
    }

    void OnLeftDown(wxMouseEvent &ev)
    {
        being_pressed_ = true;
        saved_logical_mouse_down_pos_ = ClientToLogical(FPoint(ev.GetPosition()));
        saved_view_origin_ = view_origin_;

        auto const lp = ClientToLogical(FPoint(ev.GetPosition()));
        captured_node_ = GetNodeComponentByPosition(lp);
        if(captured_node_ != nullptr) {
            BringToFront(captured_node_);

            NodeComponent2::MouseEvent nev(lp, ev);
            captured_node_->OnMouseEventFromParent(nev, &NodeComponent2::OnLeftDown);

            saved_captured_node_pos_ = captured_node_->GetPosition();

            Refresh();
            return;
        }

        wxKeyboardState mod_cut;
        mod_cut.SetShiftDown(true);

        if(ev.GetModifiers() == mod_cut.GetModifiers()) {
            cut_mode_ = true;
        }

        if(ev.HasAnyModifiers()) {
            return;
        }
    }

    struct ConnectNodeAction
    :   IUndoable
    {
        ConnectNodeAction(NodeConnectionInfo conn,
                          GraphEditor2 *owner)
        :   conn_(conn)
        ,   owner_(owner)
        {
            conn_.selected_ = false;
        }

        void perform() override
        {
            owner_->conns_.push_back(conn_);
            owner_->Refresh();
        }

        void undo() override
        {
            auto found = std::find(owner_->conns_.begin(),
                                   owner_->conns_.end(),
                                   conn_);

            assert(found != owner_->conns_.end());
            owner_->conns_.erase(found);
            owner_->Refresh();
        }

    private:
        NodeConnectionInfo conn_;
        GraphEditor2 *owner_ = nullptr;
    };

    struct DisconnectNodesAction
    :   IUndoable
    {
        DisconnectNodesAction(NodeConnectionInfo conn,
                             GraphEditor2 *owner)
        :   conn_(conn)
        ,   owner_(owner)
        {
            conn_.selected_ = false;
        }

        void perform() override
        {
            auto found = std::find(owner_->conns_.begin(),
                                   owner_->conns_.end(),
                                   conn_);

            assert(found != owner_->conns_.end());
            owner_->conns_.erase(found);
            owner_->Refresh();
        }

        void undo() override
        {
            owner_->conns_.push_back(conn_);
            owner_->Refresh();
        }

    private:
        NodeConnectionInfo conn_;
        GraphEditor2 *owner_ = nullptr;
    };

    struct MoveNodeComponentAction
    :   public IUndoable
    {
        MoveNodeComponentAction(NodeComponent2 *nc,
                                FPoint old_logical_pos,
                                FPoint new_logical_pos,
                                GraphEditor2 *owner)
        :   nc_(nc)
        ,   old_pos_(old_logical_pos)
        ,   new_pos_(new_logical_pos)
        ,   owner_(owner)
        {}

        void perform()
        {
            nc_->SetPosition(new_pos_);
            owner_->Refresh();
        }

        void undo()
        {
            nc_->SetPosition(old_pos_);
            owner_->Refresh();
        }

    private:
        NodeComponent2 *nc_ = nullptr;
        FPoint new_pos_;
        FPoint old_pos_;
        GraphEditor2 *owner_ = nullptr;
    };

    void OnLeftUp(wxMouseEvent &ev)
    {
        being_pressed_ = false;

        if(try_conn_) {
            HWM_SCOPE_EXIT([&, this] {
                Refresh(); // Need to refresh to erase provisional connection curve.
                try_conn_.reset();
            });

            auto const lp = ClientToLogical(FPoint(ev.GetPosition()));
            auto const found_node = GetNodeComponentByPosition(lp);
            if(found_node == nullptr) { return; }

            auto pos_in_node = FPoint {
                lp.x - found_node->GetPosition().x,
                lp.y - found_node->GetPosition().y
            };

            if(found_node != nullptr && found_node != try_conn_->from_) {
                bool const is_input = !try_conn_->is_input_pin_;
                auto pin = found_node->GetPinIndex(is_input, pos_in_node);
                if(pin != -1) {
                    NodeConnectionInfo conn;
                    if(try_conn_->is_input_pin_) {
                        conn.upstream_ = found_node->GetNode();
                        conn.downstream_ = try_conn_->from_->GetNode();
                        conn.output_pin_ = pin;
                        conn.input_pin_ = try_conn_->pin_index_;
                    } else {
                        conn.upstream_ = try_conn_->from_->GetNode();
                        conn.downstream_ = found_node->GetNode();
                        conn.output_pin_ = try_conn_->pin_index_;
                        conn.input_pin_ = pin;
                    }

                    if(std::find(conns_.begin(), conns_.end(), conn) == conns_.end()) {
                        auto name = L"Connect nodes [{}({}) -> {}({})]"_format(conn.upstream_->GetName(),
                                                                               conn.output_pin_,
                                                                               conn.downstream_->GetName(),
                                                                               conn.input_pin_);
                        ScopedUndoTransaction sut(name);

                        PerformAndAdd<ConnectNodeAction>(conn, this);
                    }
                }
            }

            return;
        }

        if(captured_node_ != nullptr) {
            HWM_SCOPE_EXIT([this] { captured_node_ == nullptr; });

            auto const lp = ClientToLogical(FPoint(ev.GetPosition()));

            auto new_mouse_down_pos = ClientToLogical(FPoint(ev.GetPosition()), saved_view_origin_);
            auto new_node_pos = saved_captured_node_pos_ + (new_mouse_down_pos - saved_logical_mouse_down_pos_);

            if(saved_captured_node_pos_ != new_node_pos) {
                ScopedUndoTransaction sut(L"Move node [" + captured_node_->GetNode()->GetName() + L"]");
                PerformAndAdd<MoveNodeComponentAction>(captured_node_, saved_captured_node_pos_, new_node_pos, this);
            }

            NodeComponent2::MouseEvent nev(lp, ev);
            captured_node_->OnMouseEventFromParent(nev, &NodeComponent2::OnLeftUp);
            Refresh();

            return;
        }

        if(cut_mode_) {
            ScopedUndoTransaction sut(L"Disconnect nodes");
            auto new_logical_mouse_pos = ClientToLogical(FPoint(ev.GetPosition()));

            std::vector<NodeConnectionInfo> to_remove;
            std::copy_if(conns_.begin(), conns_.end(), std::back_inserter(to_remove),
                         [](auto const &conn) { return conn.selected_; });

            for(auto const &conn: to_remove) {
                auto name = L"Disconnect node [{}({}) -> {}({})]"_format(conn.upstream_->GetName(),
                                                                         conn.output_pin_,
                                                                         conn.downstream_->GetName(),
                                                                         conn.input_pin_);
                ScopedUndoTransaction sut(name);
                PerformAndAdd<DisconnectNodesAction>(conn, this);
            }

            cut_mode_ = false;
            Refresh();
        }
    }

    void OnMouseMove(wxMouseEvent &ev)
    {
        latest_logical_mouse_pos_ = ClientToLogical(FPoint(ev.GetPosition()));

        if(cut_mode_) {
            auto select_if_intersected = [this](auto &conn) {
                auto un = GetNodeComponentByPointer(conn.upstream_);
                auto dn = GetNodeComponentByPointer(conn.downstream_);

                FPoint pt_b {
                    float(un->GetPosition().x + un->GetSize().w * (conn.output_pin_ + 1) / (un->GetNumOutputs() + 1.0)),
                    float(un->GetPosition().y + un->GetSize().h)
                };

                FPoint pt_e {
                    float(dn->GetPosition().x + dn->GetSize().w * (conn.input_pin_ + 1) / (dn->GetNumInputs() + 1.0)),
                    float(dn->GetPosition().y)
                };

                FPoint pt1 { pt_b.x, (float)((pt_b.y + pt_e.y) / 2.0) };
                FPoint pt2 { pt_e.x, (float)((pt_b.y + pt_e.y) / 2.0) };

                Bezier b;
                b.pt_begin_ = pt_b;
                b.pt_end_ = pt_e;
                b.pt_control1_ = pt1;
                b.pt_control2_ = pt2;

                conn.selected_ = b.isIntersected(saved_logical_mouse_down_pos_, latest_logical_mouse_pos_);
            };

            for(auto &conn: conns_) {
                select_if_intersected(conn);
            }

            Refresh();
            return;
        }

        if(!try_conn_ && being_pressed_ && captured_node_ == nullptr) {
            auto new_mouse_down_pos = ClientToLogical(FPoint(ev.GetPosition()), saved_view_origin_);
            auto new_view_origin = saved_view_origin_ + (new_mouse_down_pos - saved_logical_mouse_down_pos_);
            if(new_view_origin != view_origin_) {
                view_origin_ = new_view_origin;
                Refresh();
            }

            return;
        }

        if(!try_conn_ && being_pressed_ && captured_node_) {
            // Node を移動する。
            auto new_mouse_down_pos = ClientToLogical(FPoint(ev.GetPosition()), saved_view_origin_);
            auto new_node_pos = saved_captured_node_pos_ + (new_mouse_down_pos - saved_logical_mouse_down_pos_);
            captured_node_->SetPosition(new_node_pos);
            Refresh();
            return;
        }

        auto const lp = ClientToLogical(FPoint(ev.GetPosition()));

        NodeComponent2 *hover_target = nullptr;

        if(captured_node_ && being_pressed_) {
            hover_target = captured_node_;
        } else {
            hover_target = GetNodeComponentByPosition(lp);
        }

        if(last_hover_target_ && last_hover_target_ != hover_target) {
            NodeComponent2::MouseEvent nev(lp, ev);
            last_hover_target_->OnMouseEventFromParent(nev, &NodeComponent2::OnMouseLeave);

            Refresh(); // TODO: node 側でやる
        }

        if(hover_target && hover_target != last_hover_target_) {
            NodeComponent2::MouseEvent nev(lp, ev);
            hover_target->OnMouseEventFromParent(nev, &NodeComponent2::OnMouseEnter);

            Refresh(); // TODO: node 側でやる
        }

        if(hover_target) {
            NodeComponent2::MouseEvent nev(lp, ev);
            hover_target->OnMouseEventFromParent(nev, &NodeComponent2::OnMouseMove);

            Refresh(); // TODO: node 側でやる
        }

        last_hover_target_ = hover_target;
    }

    NodeComponent2 * GetNodeComponentByPosition(FPoint logical_pos) const noexcept
    {
        auto found = std::find_if(nodes_.begin(),
                                  nodes_.end(),
                                  [logical_pos](auto &node) { return node->GetRect().Contain(logical_pos); });

        if(found != nodes_.end()) {
            return found->get();
        }

        return nullptr;
    }

    NodeComponent2 * GetNodeComponentByPointer(Node *node) const noexcept
    {
        auto found = std::find_if(nodes_.begin(),
                                  nodes_.end(),
                                  [node](auto &nc) { return nc->GetNode() == node; });

        if(found != nodes_.end()) {
            return found->get();
        }

        return nullptr;
    }

    void OnRightUp(wxMouseEvent &ev)
    {
        auto cp = ev.GetPosition();
        auto lp = ClientToLogical(FPoint(cp.x, cp.y));

        if(auto node = GetNodeComponentByPosition(lp)) {
            NodeComponent2::MouseEvent nev(lp, ev);
            node->OnMouseEventFromParent(nev, &NodeComponent2::OnRightUp);
            return;
        }

        wxMenu menu;

        static int const kAddNode = 1000;

        menu.Append(kAddNode, "&AddNode\tCTRL-a", "Add a node");

        menu.Bind(wxEVT_MENU, [cp, this](wxCommandEvent &ev) {
            if(ev.GetId() == kAddNode) {
                AddNode(cp);
            }
        });

        PopupMenu(&menu);
    }

    void OnMouseWheel(wxMouseEvent &ev)
    {
        auto cp = ev.GetPosition();
        auto lp = ClientToLogical(FPoint(cp.x, cp.y));

        if(auto node = GetNodeComponentByPosition(lp)) {
            NodeComponent2::MouseEvent nev(lp, ev, ev.GetWheelDelta(), ev.GetWheelRotation());
            node->OnMouseEventFromParent(nev, &NodeComponent2::OnMouseWheel);
            return;
        }

        double const kMouseZoomChangeRatio = pow(1.5, 1/9.0);
        if(ev.GetWheelRotation() > 0) {
            auto const new_scale = zoom_factor_ * pow(kMouseZoomChangeRatio, fabs(ev.GetWheelRotation() / (double)ev.GetWheelDelta()));
            if(new_scale < 5.0) {
                zoom_factor_ = new_scale;
            }
        } else {
            auto const new_scale = zoom_factor_ / pow(kMouseZoomChangeRatio, fabs(ev.GetWheelRotation() / (double)ev.GetWheelDelta()));
            if(new_scale > 0.03) {
                zoom_factor_ = new_scale;
            }
        }

        Refresh();
    }

    void OnCaptureLost(wxMouseCaptureLostEvent &ev)
    {
        being_pressed_ = false;
    }

    void OnKeyUp(wxKeyEvent &ev)
    {
        double kZoomChangeRatio = 1.5;

        if(ev.GetKeyCode() == WXK_UP) {
            // 拡大
            auto const new_scale = zoom_factor_ * kZoomChangeRatio;
            if(new_scale < 5.0) {
                zoom_factor_ = new_scale;
            }

        } else if(ev.GetKeyCode() == WXK_DOWN) {
            // 縮小
            auto const new_scale = zoom_factor_ / kZoomChangeRatio;
            if(new_scale > 0.03) {
                zoom_factor_ = new_scale;
            }
        }

        if(ev.GetKeyCode() == WXK_SPACE) {
            view_origin_.x = 0;
            view_origin_.y = 0;
        }

        if(ev.GetUnicodeKey() == 'W') {
            view_origin_.y -= 10;
        } else if(ev.GetUnicodeKey() == 'S') {
            view_origin_.y += 10;
        } else if(ev.GetUnicodeKey() == 'A') {
            view_origin_.x -= 10;
        } else if(ev.GetUnicodeKey() == 'D') {
            view_origin_.x += 10;
        }
        Refresh();
    }

    struct AddNodeAction
    :   public IUndoable
    {
        AddNodeAction(NodePtr p,
                      FPoint logical_point,
                      GraphEditor2 *owner)
        :   p_(std::move(p))
        ,   logical_point_(logical_point)
        ,   owner_(owner)
        {
            p_raw_ = p_.get();

            nc_ = std::make_unique<NodeComponent2>(owner_, p_raw_);
            nc_->SetPosition(logical_point_);
            nc_->SetSize(GetDefaultNodeSize());

            nc_raw_ = nc_.get();
        }

        void perform() override
        {
            owner_->node_container_->AddNode(std::move(p_));
            owner_->nodes_.insert(owner_->nodes_.begin(), std::move(nc_));

            owner_->Refresh();
        }

        void undo() override
        {
            assert(owner_->GetUpstreamSideConnections(p_raw_).empty());
            assert(owner_->GetDownstreamSideConnections(p_raw_).empty());

            auto found_nc = std::find_if(owner_->nodes_.begin(),
                                         owner_->nodes_.end(),
                                         [nc_raw = nc_raw_](auto const &nc) { return nc.get() == nc_raw; }
                                         );

            assert(found_nc != owner_->nodes_.end());

            nc_ = std::move(*found_nc);
            owner_->nodes_.erase(found_nc);
            p_ = owner_->node_container_->RemoveNode(p_raw_);
            assert(p_);

            if(owner_->captured_node_ == nc_raw_) { owner_->captured_node_ = nullptr; }
            if(owner_->last_hover_target_ == nc_raw_) { owner_->last_hover_target_ = nullptr; }
            owner_->Refresh();
        }

    private:
        NodePtr p_;
        Node *p_raw_ = nullptr;
        NodeComponent2Ptr nc_;
        NodeComponent2 *nc_raw_ = nullptr;
        FPoint logical_point_;
        GraphEditor2 *owner_ = nullptr;
    };

    void AddNode(wxPoint pt)
    {
        ScopedUndoTransaction sut(L"Add node");
        static int num;
        std::wstringstream ss;
        ss << L"Node[" << num << L"]";
        num++;

        auto node = std::make_unique<Node>(ss.str(), 3, 4);

        auto lp = ClientToLogical(FPoint(pt));
        auto const def = GetDefaultNodeSize();
        lp.x -= def.w / 2;
        lp.y -= def.h / 2;

        PerformAndAdd<AddNodeAction>(std::move(node),
                                     lp,
                                     this);
    }

    std::vector<NodeConnectionInfo> GetUpstreamSideConnections(Node const *p) const
    {
        std::vector<NodeConnectionInfo> ret;
        std::copy_if(conns_.begin(),
                     conns_.end(),
                     std::back_inserter(ret),
                     [p](auto const &conn) { return conn.upstream_ == p; });

        return ret;
    }

    std::vector<NodeConnectionInfo> GetDownstreamSideConnections(Node const *p) const
    {
        std::vector<NodeConnectionInfo> ret;
        std::copy_if(conns_.begin(),
                     conns_.end(),
                     std::back_inserter(ret),
                     [p](auto const &conn) { return conn.downstream_ == p; });

        return ret;
    }

    void TryToConnect(NodeComponent2 *node, bool is_input_pin, int pin_index) override
    {
        TryToConnectInfo tci;
        tci.from_ = node;
        tci.is_input_pin_ = is_input_pin;
        tci.pin_index_ = pin_index;

        try_conn_ = tci;
    }

    void SetGraph(GraphProcessor &graph)
    {
        graph_ = &graph;
    }

    double GetZoomFactor() const noexcept { return zoom_factor_; }
    FPoint GetViewOrigin() const noexcept { return view_origin_; }

    auto FindNodeComponent(NodeComponent2 *p) {
        return std::find_if(nodes_.begin(),
                            nodes_.end(),
                            [p](auto const &nc) { return nc.get() == p; });
    }

    auto FindNodeComponent(NodeComponent2 *p) const {
        return std::find_if(nodes_.begin(),
                            nodes_.end(),
                            [p](auto const &nc) { return nc.get() == p; });
    }

    struct DeleteNodeAction
    :   public IUndoable
    {
        DeleteNodeAction(Node *node,
                         GraphEditor2 *owner)
        :   p_raw_(node)
        ,   owner_(owner)
        {
            nc_raw_ = owner_->GetNodeComponentByPointer(p_raw_);
            auto up_conns = owner_->GetUpstreamSideConnections(p_raw_);
            auto down_conns = owner_->GetDownstreamSideConnections(p_raw_);

            stored_conns_.insert(stored_conns_.end(), up_conns.begin(), up_conns.end());
            stored_conns_.insert(stored_conns_.end(), down_conns.begin(), down_conns.end());
        }

        void perform() override
        {
            std::vector<NodeConnectionInfo> tmp;
            std::copy_if(owner_->conns_.begin(),
                         owner_->conns_.end(),
                         std::back_inserter(tmp),
                         [p = p_raw_](auto const &conn) { return conn.upstream_ != p && conn.downstream_ != p; }
                         );

            std::swap(tmp, owner_->conns_);

            auto nc_found = owner_->FindNodeComponent(nc_raw_);
            assert(nc_found != owner_->nodes_.end());

            nc_ = std::move(*nc_found);
            owner_->nodes_.erase(nc_found);

            p_ = owner_->node_container_->RemoveNode(p_raw_);
            if(owner_->captured_node_ == nc_raw_) { owner_->captured_node_ = nullptr; }
            if(owner_->last_hover_target_ == nc_raw_) { owner_->last_hover_target_ = nullptr; }

            owner_->Refresh();
        }

        void undo() override
        {
            owner_->node_container_->AddNode(std::move(p_));
            owner_->nodes_.insert(owner_->nodes_.begin(), std::move(nc_));
            std::copy(stored_conns_.begin(), stored_conns_.end(), std::back_inserter(owner_->conns_));

            owner_->Refresh();
        }

    private:
        Node *p_raw_ = nullptr;
        NodePtr p_;
        NodeComponent2 *nc_raw_ = nullptr;
        NodeComponent2Ptr nc_;
        GraphEditor2 *owner_ = nullptr;
        std::vector<NodeConnectionInfo> stored_conns_;
    };

    void DeleteNode(NodeComponent2 *nc) override
    {
        assert(nc != nullptr);

        ScopedUndoTransaction sut(L"Delete node");

        PerformAndAdd<DeleteNodeAction>(nc->GetNode(), this);
    }

    void ConnectNodes(Node *upstream, Node *downstream)
    {

    }

    bool ShowNodePopup(wxMenu &menu) override
    {
        return PopupMenu(&menu);
    }

private:
    GraphProcessor *graph_ = nullptr;
    double zoom_factor_ = 1.0; // 数値が大きいほうが拡大率が高い（ViewWindow が小さい）
    std::vector<NodeComponent2Ptr> nodes_;
    FPoint view_origin_;
    bool being_pressed_ = false;
    bool cut_mode_ = false;
    NodeComponent2 *captured_node_ = nullptr;
    NodeComponent2 *last_hover_target_ = nullptr;
    std::optional<TryToConnectInfo> try_conn_;

    //! view origin position at dragging started.
    FPoint saved_view_origin_;
    FPoint saved_captured_node_pos_;

    //! mouse down position at dragging started.
    FPoint saved_logical_mouse_down_pos_;
    FPoint latest_logical_mouse_pos_;
    GraphicsBuffer gb_;

    std::vector<NodeConnectionInfo> conns_;
    std::unique_ptr<NodeContainer> node_container_;

    void RearrangeNodes() override
    {}
};

std::unique_ptr<IGraphEditor> CreateGraphEditor2Component(wxWindow *parent, GraphProcessor &graph)
{
    auto p = std::make_unique<GraphEditor2>(parent, wxID_ANY);
    p->SetGraph(graph);

    return p;
}

NS_HWM_END
