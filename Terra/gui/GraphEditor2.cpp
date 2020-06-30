#include "GraphEditor2.hpp"

#include "../App.hpp"
#include "./PluginEditor.hpp"
#include "./DataType.hpp"
#include "./Button.hpp"
#include "./Util.hpp"
#include "../Misc/ScopeExit.hpp"

NS_HWM_BEGIN

bool isLinesIntersected(FPoint line1_begin, FPoint line1_end,
                        FPoint line2_begin, FPoint line2_end)
{
    auto const &a = line1_begin;
    auto const &b = line1_end;
    auto const &c = line2_begin;
    auto const &d = line2_end;

    auto const ta = (c.x - d.x) * (a.y - c.y) + (c.y - d.y) * (c.x - a.x);
    auto const tb = (c.x - d.x) * (b.y - c.y) + (c.y - d.y) * (c.x - b.x);
    auto const tc = (a.x - b.x) * (c.y - a.y) + (a.y - b.y) * (a.x - c.x);
    auto const td = (a.x - b.x) * (d.y - a.y) + (a.y - b.y) * (a.x - d.x);

    return tc * td < 0 && ta * tb < 0;
}

struct Bezier
{
    Bezier()
    {}

    FPoint get(float t) const
    {
        assert(0.0 <= t && t <= 1.0);
        auto const &p1 = pt_begin_;
        auto const &p2 = pt_control1_;
        auto const &p3 = pt_control2_;
        auto const &p4 = pt_end_;

        auto cube = [](auto x) { return x * x * x; };
        auto square = [](auto x) { return x * x; };
        return FPoint {
            cube(1 - t) * p1.x + 3 * square(1 - t) * t * p2.x + 3 * (1 - t) * square(t) * p3.x + cube(t) * p4.x,
            cube(1 - t) * p1.y + 3 * square(1 - t) * t * p2.y + 3 * (1 - t) * square(t) * p3.y + cube(t) * p4.y,
        };
    }

    bool isIntersected(FPoint line_begin, FPoint line_end) const
    {
        int const kNumMaxCompletion = 3000;

        // pt1 と pt2 で表す直線がベジエ曲線の凸包と交差するかどうか。
        bool is_intersected_convex_hull = true;

        if(is_intersected_convex_hull == false) { return false; }

        auto diff = line_end - line_begin;
        auto const num_compl = std::min<int>(std::max<int>(fabs(diff.w), fabs(diff.h)), kNumMaxCompletion);

        auto pt_last = get(0);
        for(int i = 1; i <= num_compl; ++i) {
            auto pt = get(i / (double)num_compl);

            if(isLinesIntersected(line_begin, line_end, pt_last, pt)) {
                return true;
            }
        }

        return false;
    }

    FPoint pt_begin_;
    FPoint pt_end_;

    FPoint pt_control1_;
    FPoint pt_control2_;
};

struct [[nodiscard]] ScopedTranslateDC
{
    ScopedTranslateDC(wxDC &dc, FSize size, bool device_origin = false)
    :   dc_(&dc)
    {
        if(device_origin) {
            dc_->GetDeviceOrigin(&saved_pos_x_, &saved_pos_y_);
            dc_->SetDeviceOrigin(saved_pos_x_ + size.w,
                                  saved_pos_y_ + size.h);
        } else {
            dc_->GetLogicalOrigin(&saved_pos_x_, &saved_pos_y_);
            dc_->SetLogicalOrigin(saved_pos_x_ + size.w,
                                  saved_pos_y_ + size.h);
        }
    }

    ScopedTranslateDC(ScopedTranslateDC const &rhs) = delete;
    ScopedTranslateDC & operator=(ScopedTranslateDC const &rhs) = delete;
    ScopedTranslateDC(ScopedTranslateDC &&rhs) = delete;
    ScopedTranslateDC & operator=(ScopedTranslateDC &&rhs) = delete;

    ~ScopedTranslateDC()
    {
        reset();
    }

    void reset()
    {
        if(!dc_) { return; }
        dc_->SetLogicalOrigin(saved_pos_x_, saved_pos_y_);
        dc_ = nullptr;
    }

private:
    wxDC *dc_ = nullptr;
    int saved_pos_x_ = 0;
    int saved_pos_y_ = 0;
};

struct [[nodiscard]] ScopedScaleDC
{
    ScopedScaleDC(wxDC &dc, double scale)
    :   ScopedScaleDC(dc, scale, scale)
    {}

    ScopedScaleDC(wxDC &dc, double scale_x, double scale_y)
    :   dc_(&dc)
    {
        dc_->GetUserScale(&saved_scale_x_, &saved_scale_y_);
        dc_->SetUserScale(scale_x, scale_y);
    }

    ScopedScaleDC(ScopedScaleDC const &rhs) = delete;
    ScopedScaleDC & operator=(ScopedScaleDC const &rhs) = delete;
    ScopedScaleDC(ScopedScaleDC &&rhs) = delete;
    ScopedScaleDC & operator=(ScopedScaleDC &&rhs) = delete;

    ~ScopedScaleDC()
    {
        reset();
    }

    void reset()
    {
        if(!dc_) { return; }
        dc_->SetUserScale(saved_scale_x_, saved_scale_y_);
        dc_ = nullptr;
    }

private:
    wxDC *dc_ = nullptr;
    double saved_scale_x_ = 1.0;
    double saved_scale_y_ = 1.0;
};

class NodeComponent2
{
    constexpr static int kShadowRadius = 3;
public:
    NodeComponent2(wxWindow *parent, int id, String const &name,
                   FPoint pos = {-1, -1},
                   FSize size = {-1, -1})
    :   parent_(parent)
    ,   id_(id)
    ,   name_(name)
    ,   rc_()
    {
        theme_ = Button::getDefaultButtonTheme();

        SetPosition(pos);
        SetSize(size);
    }

    void SetInputs(int num)
    {
        num_inputs_ = num;
    }

    void SetOutput(int num)
    {
        num_outputs_ = num;
    }

    int GetNumInputs() const { return num_inputs_; }
    int GetNumOutputs() const { return num_outputs_; }

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

        float const kPinRadius = 4;

        // draw input pins
        for(int i = 0, end = num_inputs_; i < end; ++i) {
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
        for(int i = 0, end = num_outputs_; i < end; ++i) {
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

    wxString GetLabel() const { return name_; }

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

    void OnLeftDown(FPoint pt, wxKeyboardState const &key)
    {
        being_pushed_ = true;
    }

    void OnLeftUp(FPoint pt, wxKeyboardState const &key)
    {
        being_pushed_ = false;
    }

    void OnRightDown(FPoint pt, wxKeyboardState const &key)
    {

    }

    void OnRightUp(FPoint pt, wxKeyboardState const &key)
    {

    }

    void OnMouseMove(FPoint pt, wxKeyboardState const &key)
    {
        hover_ = (GetClientRect().Contain(pt));
    }

    void OnMouseEnter(FPoint pt, wxKeyboardState const &key)
    {
        hover_ = true;
    }

    void OnMouseLeave(FPoint pt, wxKeyboardState const &key)
    {
        hover_ = false;
    }

    int GetId() const noexcept { return id_; }

private:
    double hue_ = 0.3;
    wxWindow *parent_ = nullptr;
    String name_;
    FRect rc_; // 親ウィンドウ上での位置
    FRect rc_shadow_;
    Button::ButtonTheme theme_;
    //GraphicsBuffer gb_shadow_;
    wxImage img_shadow_;

    int num_inputs_ = 0;
    int num_outputs_ = 0;

    bool being_pushed_ = false;
    bool pushed_ = false;
    bool hover_ = false;

    int id_ = -1;

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
{
public:
    using NodeComponent2Ptr = std::unique_ptr<NodeComponent2>;

    struct NodeConnectionInfo
    {
        NodeComponent2 *upstream_;
        NodeComponent2 *downstream_;

        int output_pin_; // upstream から出力するピンの番号
        int input_pin_;  // downtream から出力するピンの番号

        bool selected_ = false;
    };

    GraphEditor2(wxWindow *parent, wxWindowID id)
    :   IGraphEditor(parent, id)
    {
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

        auto draw_connection = [&](NodeConnectionInfo const &conn) {
            auto &un = conn.upstream_;
            auto &dn = conn.downstream_;

            auto path = gc->CreatePath();

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
            auto *node = nodes_[end - i - 1].get();
            auto const node_shift = node->GetPosition();

            ScopedTranslateDC tdc(dc, FSize { -node_shift.x, -node_shift.y });

            node->OnPaint(dc);

            tdc.reset();

            auto conns = GetUpstreamSideConnections(node);

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

//        for(int i = 0, end = nodes_.size(); i < end; ++i) {
//            auto &node = nodes_[end - i - 1];
//
//            auto pair_node = find_node(node->GetId() + 1);
//            if(pair_node == nullptr) { continue; }
//
//            auto path = gc->CreatePath();
//
//            wxPoint2DDouble pt_b {
//                node->GetPosition().x + node->GetSize().w / 2,
//                node->GetPosition().y + node->GetSize().h
//            };
//
//            wxPoint2DDouble pt_e {
//                pair_node->GetPosition().x + pair_node->GetSize().w / 2,
//                pair_node->GetPosition().y
//            };
//
//            wxPoint2DDouble pt1 { pt_b.m_x, (pt_b.m_y + pt_e.m_y) / 2.0 };
//            wxPoint2DDouble pt2 { pt_e.m_x, (pt_b.m_y + pt_e.m_y) / 2.0 };
//
//            path.MoveToPoint(pt_b);
//            path.AddCurveToPoint(pt1, pt2, pt_e);
//
//            gc->SetPen(wxPen(HSVToColour(0.2, 0.6, 0.8, 0.6), 2.0));
//            gc->DrawPath(path);
//
//            // 自前でベジエ曲線を描画
//
//            Bezier b;
//            b.pt_begin_     = FPoint(pt_b.m_x, pt_b.m_y);
//            b.pt_end_       = FPoint(pt_e.m_x, pt_e.m_y);
//            b.pt_control1_  = FPoint(pt1.m_x, pt1.m_y);
//            b.pt_control2_  = FPoint(pt2.m_x, pt2.m_y);
//
//            int const kNumCompletion = 3000;
//            auto last_pt = b.get(0);
//            for(int ti = 1; ti <= kNumCompletion; ++ti) {
//                auto bezier_pt1 = last_pt;
//                auto bezier_pt2 = b.get(ti / (double)kNumCompletion);
//
//                wxPoint2DDouble points[2] = {
//                    { bezier_pt1.x + 4, bezier_pt1.y },
//                    { bezier_pt2.x + 4, bezier_pt2.y }
//                };
//
//                gc->SetPen(wxPen(HSVToColour(0.7, 0.6, 0.8, 0.6), 2.0, wxPENSTYLE_SOLID));
//                gc->StrokeLine(points[0].m_x, points[0].m_y, points[1].m_x, points[1].m_y);
//
//                last_pt = bezier_pt2;
//            }
//        }

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
        captured_node_ = GetNodeByPosition(lp);
        if(captured_node_ != nullptr) {
            BringToFront(captured_node_);

            auto pos_in_node = FPoint {
                lp.x - captured_node_->GetPosition().x,
                lp.y - captured_node_->GetPosition().y
            };

            captured_node_->OnLeftDown(pos_in_node, ev);
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

    void OnLeftUp(wxMouseEvent &ev)
    {
        being_pressed_ = false;

        if(captured_node_ != nullptr) {
            auto const lp = ClientToLogical(FPoint(ev.GetPosition()));
            auto pos_in_node = FPoint {
                lp.x - captured_node_->GetPosition().x,
                lp.y - captured_node_->GetPosition().y
            };

            captured_node_->OnLeftUp(pos_in_node, ev);

            Refresh();
            return;
        }

        if(cut_mode_) {
            auto new_logical_mouse_pos = ClientToLogical(FPoint(ev.GetPosition()));

            std::vector<NodeConnectionInfo> tmp;
            std::copy_if(conns_.begin(), conns_.end(), std::back_inserter(tmp),
                         [](auto const &conn) { return conn.selected_ == false; });

            std::swap(tmp, conns_);
            cut_mode_ = false;
            Refresh();
        }
    }

    void OnMouseMove(wxMouseEvent &ev)
    {
        latest_logical_mouse_pos_ = ClientToLogical(FPoint(ev.GetPosition()));

        if(cut_mode_) {
            auto select_if_intersected = [this](auto &conn) {
                auto &un = conn.upstream_;
                auto &dn = conn.downstream_;

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

        if(being_pressed_ && captured_node_ == nullptr) {
            auto new_mouse_down_pos = ClientToLogical(FPoint(ev.GetPosition()), saved_view_origin_);
            auto new_view_origin = saved_view_origin_ + (new_mouse_down_pos - saved_logical_mouse_down_pos_);
            if(new_view_origin != view_origin_) {
                view_origin_ = new_view_origin;
                Refresh();
            }

            return;
        }

        if(being_pressed_ && captured_node_) {
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
            hover_target = GetNodeByPosition(lp);
        }

        if(last_hover_target_ && last_hover_target_ != hover_target) {
            auto pos_in_node = FPoint {
                lp.x - last_hover_target_->GetPosition().x,
                lp.y - last_hover_target_->GetPosition().y
            };

            last_hover_target_->OnMouseLeave(pos_in_node, ev);
            Refresh(); // TODO: node 側でやる
        }

        if(hover_target && hover_target != last_hover_target_) {
            auto pos_in_node = FPoint {
                lp.x - hover_target->GetPosition().x,
                lp.y - hover_target->GetPosition().y
            };

            hover_target->OnMouseEnter(pos_in_node, ev);
            Refresh(); // TODO: node 側でやる
        }

        if(hover_target) {
            auto pos_in_node = FPoint {
                lp.x - hover_target->GetPosition().x,
                lp.y - hover_target->GetPosition().y
            };

            hover_target->OnMouseMove(pos_in_node, ev);
            Refresh(); // TODO: node 側でやる
        }

        last_hover_target_ = hover_target;
    }

    NodeComponent2 * GetNodeByPosition(FPoint logical_pos) const noexcept
    {
        auto found = std::find_if(nodes_.begin(),
                                  nodes_.end(),
                                  [logical_pos](auto &node) { return node->GetRect().Contain(logical_pos); });

        if(found != nodes_.end()) {
            return found->get();
        }

        return nullptr;
    }

    NodeComponent2 * GetNodeById(int id) const noexcept
    {
        auto found = std::find_if(nodes_.begin(),
                                  nodes_.end(),
                                  [id](auto &node) { return node->GetId() == id; });

        if(found != nodes_.end()) {
            return found->get();
        }

        return nullptr;
    }

    void OnRightUp(wxMouseEvent &ev)
    {
        auto pos = ev.GetPosition();
        wxMenu menu;

        static int const kAddNode = 1000;

        menu.Append(kAddNode, "&AddNode\tCTRL-a", "Add a node");

        menu.Bind(wxEVT_MENU, [pos, this](wxCommandEvent &ev) {
            if(ev.GetId() == kAddNode) {
                AddNode(pos);
            }
        });

        PopupMenu(&menu);
    }

    void OnMouseWheel(wxMouseEvent &ev)
    {
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

    void AddNode(wxPoint pt)
    {
        static int num;
        std::wstringstream ss;
        ss << L"Node[" << num << L"]";
        auto node = std::make_unique<NodeComponent2>(this, num, ss.str());
        num++;
        node->SetInputs(3);
        node->SetOutput(4);

        node->SetPosition(ClientToLogical(FPoint(pt)));
        node->SetSize(FSize{180, 60});

        if(auto downstream_node = GetNodeById(node->GetId() / 2)) {
            NodeConnectionInfo ci;
            ci.upstream_ = node.get();
            ci.downstream_ = downstream_node;
            ci.output_pin_ = num % ci.upstream_->GetNumOutputs();
            ci.input_pin_ = num % ci.downstream_->GetNumInputs();

            conns_.push_back(ci);
        }

        nodes_.insert(nodes_.begin(), std::move(node));

        Refresh();
    }

    std::vector<NodeConnectionInfo> GetUpstreamSideConnections(NodeComponent2 *p) const
    {
        std::vector<NodeConnectionInfo> ret;
        std::copy_if(conns_.begin(),
                     conns_.end(),
                     std::back_inserter(ret),
                     [p](auto const &conn) { return conn.upstream_ == p; });

        return ret;
    }

    std::vector<NodeConnectionInfo> GetDownstreamSideConnections(NodeComponent2 *p) const
    {
        std::vector<NodeConnectionInfo> ret;
        std::copy_if(conns_.begin(),
                     conns_.end(),
                     std::back_inserter(ret),
                     [p](auto const &conn) { return conn.downstream_ == p; });

        return ret;
    }

    void SetGraph(GraphProcessor &graph)
    {
        graph_ = &graph;
    }

    double GetZoomFactor() const noexcept { return zoom_factor_; }
    FPoint GetViewOrigin() const noexcept { return view_origin_; }

private:
    GraphProcessor *graph_ = nullptr;
    double zoom_factor_ = 1.0; // 数値が大きいほうが拡大率が高い（ViewWindow が小さい）
    std::vector<NodeComponent2Ptr> nodes_;
    FPoint view_origin_;
    bool being_pressed_ = false;
    bool cut_mode_ = false;
    NodeComponent2 *captured_node_ = nullptr;
    NodeComponent2 *last_hover_target_ = nullptr;

    //! view origin position at dragging started.
    FPoint saved_view_origin_;
    FPoint saved_captured_node_pos_;

    //! mouse down position at dragging started.
    FPoint saved_logical_mouse_down_pos_;
    FPoint latest_logical_mouse_pos_;
    GraphicsBuffer gb_;

    std::vector<NodeConnectionInfo> conns_;

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
