#include "Slider.hpp"
#include "../misc/ScopeExit.hpp"

NS_HWM_BEGIN

struct Slider::Impl
{
    Impl(Slider *owner, float value_min, float value_max, float value_default)
    :   owner_(owner)
    ,   value_min_(value_min)
    ,   value_max_(value_max)
    ,   value_default_(value_default)
    ,   value_(value_default_)
    {
        assert(value_min_ <= value_default_ && value_default_ <= value_max_);
    }

    Slider *owner_ = nullptr;
    float value_max_ = 0;
    float value_min_ = 0;
    float value_default_ = 0;
    float value_ = 0;
    bool being_pushed_ = false;
    bool hover_ = false;
    bool hover_thumb_ = false;
    float hue_ = 0;

    FPoint saved_mouse_down_pos_;
    FSize saved_mouse_pos_diff_; // LeftDown 時のマウス中心からの距離

    constexpr static float kThumbRadius = 6;
    constexpr static float kBarRadius = 3;

    FPoint GetThumbCenter() const noexcept
    {
        auto rc = owner_->GetClientRect().Deflated(kThumbRadius, 0);
        return FPoint {
            (float)((value_ - value_min_) / (value_max_ - value_min_) * rc.GetWidth() + kThumbRadius),
            (float)(rc.GetHeight() / 2.0)
        };
    }

    bool IsOnThumb(FPoint pt) const noexcept {
        auto const center = GetThumbCenter();
        auto dx = pt.x - center.x - kThumbRadius;
        auto dy = pt.y - center.y;

        return sqrt(dx * dx + dy * dy) <= kThumbRadius;
    }

    float XToValue(float x_pos) const noexcept
    {
        auto w = owner_->GetWidth() - kThumbRadius * 2;
        x_pos -= kThumbRadius;
        x_pos = std::clamp<float>(x_pos, 0.0f, w);
        return (x_pos / w) * (value_max_ - value_min_) + value_min_;
    }

    float ValueToX(float value) const noexcept
    {
        auto w = owner_->GetWidth() - kThumbRadius * 2;
        value = std::clamp<float>(value, value_min_, value_max_);
        return (value - value_min_) / (value_max_ - value_min_) * w + kThumbRadius;
    }
};

Slider::Slider(FPoint pos, FSize size)
:   Slider(0.0, 1.0, 0.5, pos, size)
{}

Slider::Slider(float value_min, float value_max, float value_default,
               FPoint pos, FSize size)
:   IWidget(pos, size)
,   pimpl_(std::make_unique<Impl>(this, value_min, value_max, value_default))
{}

Slider::~Slider()
{}

float Slider::GetHue() const noexcept { return pimpl_->hue_; }
void Slider::SetHue(float hue) noexcept { pimpl_->hue_ = hue; }

void Slider::OnLeftDown(MouseEvent &ev)
{
    hwm::dout << __PRETTY_FUNCTION__ << std::endl;

    pimpl_->being_pushed_ = true;
    pimpl_->saved_mouse_down_pos_ = ev.pt_;

    if(pimpl_->IsOnThumb(ev.pt_) == false) {
        pimpl_->value_ = pimpl_->XToValue(ev.pt_.x);
        wxCommandEvent ev(wxEVT_SLIDER);
        ev.SetEventObject(this);
        ProcessEvent(ev);
        Refresh();
    }

    auto center = pimpl_->GetThumbCenter();
    pimpl_->saved_mouse_pos_diff_ = (ev.pt_ - center);
    CaptureMouse();
}

void Slider::OnLeftUp(MouseEvent &ev)
{
    hwm::dout << __PRETTY_FUNCTION__ << std::endl;

    HWM_SCOPE_EXIT([&, this] {
        pimpl_->being_pushed_ = false;
    });

    ReleaseMouse();
}

void Slider::OnMouseMove(MouseEvent &ev)
{
    hwm::dout << __PRETTY_FUNCTION__ << std::endl;

    if(pimpl_->being_pushed_) {
        auto new_center = ev.pt_ - pimpl_->saved_mouse_pos_diff_;
        pimpl_->value_ = pimpl_->XToValue(ev.pt_.x);
        wxCommandEvent ev(wxEVT_SLIDER);
        ev.SetEventObject(this);
        ProcessEvent(ev);

        Refresh();
    }
}

void Slider::OnPaint(wxDC &dc)
{
    auto const br = Impl::kBarRadius;
    auto const tr = Impl::kThumbRadius;

    auto rc = GetClientRect().Deflated(tr, 0);
    auto const rc_bar_frame = FRect {
        FPoint { rc.GetX(), rc.GetHeight() / 2 - br - 1 },
        FPoint { rc.GetX() + rc.GetWidth(), rc.GetHeight() / 2 + br + 1 },
    };

    auto const trans_pen = wxTransparentColour;

    // draw slider bar edge
    auto bp = BrushPen { HSVToColour(0, 0, 0, 0.6), trans_pen };
    bp.ApplyTo(dc);
    dc.DrawRoundedRectangle(rc_bar_frame, br);

    auto rc_bar_inner = rc_bar_frame.Deflated(1);
    // draw slider bar shadow
    bp = BrushPen { HSVToColour(0, 0, 0.7, 0.7), trans_pen };
    bp.ApplyTo(dc);
    dc.DrawRoundedRectangle(rc_bar_inner, br);

    // draw slider bar
    bp = BrushPen { HSVToColour(0, 0, 0.9), trans_pen };
    bp.ApplyTo(dc);
    dc.DrawRoundedRectangle(rc_bar_inner
                            .WithSize(rc_bar_inner.GetWidth(), rc_bar_inner.GetHeight()-1)
                            .Translated(0, 1), br);

    // draw thumb shadow
    bp = BrushPen { HSVToColour(pimpl_->hue_, 0.4, 0.88f), HSVToColour(0.0, 0.0, 0.2, 0.3) };
    bp.ApplyTo(dc);
    dc.DrawCircle(pimpl_->GetThumbCenter().Translated(0, 1), tr-1);

    // draw thumb
    bp = BrushPen { HSVToColour(pimpl_->hue_, 0.4, 0.88f), HSVToColour(0.0, 0.0, 0.2, 0.3) };
    bp.ApplyTo(dc);
    dc.DrawCircle(pimpl_->GetThumbCenter(), tr-1);
}

void Slider::OnMouseEnter(MouseEvent &ev)
{
    pimpl_->hover_ = true;
}

void Slider::OnMouseLeave(MouseEvent &ev)
{
    pimpl_->hover_ = false;
}

NS_HWM_END
