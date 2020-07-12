//
//  Button.cpp
//  Terra
//
//  Created by yuasa on 2020/06/27.
//

#include "Button.hpp"
#include "Util.hpp"
#include <wx/graphics.h>
#include "../misc/ScopeExit.hpp"

NS_HWM_BEGIN

Button::ButtonTheme Button::getDefaultButtonTheme() noexcept
{
    return {
        6.0f, 1.0f,
        { 0.5f, 0.4f, 0.1f, 0.92f, 0.72f, 0.66f, 0.15f, 0.9f },
        { 0.5f, 0.4f, 0.1f, 0.97f, 0.82f, 0.76f, 0.15f, 0.9f },
        { 0.5f, 0.4f, 0.1f, 0.32f, 0.54f, 0.48f, 0.15f, 0.9f },
    };
}

Button::Button(FPoint pos,
               FSize size,
               ButtonTheme theme)
:   IWidget(pos, size)
,   theme_(theme)
{}

void Button::setHue(float hue) noexcept
{
    assert(0.0f <= hue_ && hue_ <= 1.0f);
    hue_ = hue;
}

float Button::getHue() const noexcept
{
    return hue_;
}

void Button::setButtonTheme(Button::ButtonTheme const &theme) noexcept
{
    theme_ = theme;
}

Button::ButtonTheme Button::getButtonTheme() const noexcept
{
    return theme_;
}

void Button::EnableToggleMode(bool to_enable) noexcept
{

    if(to_enable == false && IsPushed()) {
        pushed_ = false;

        wxCommandEvent evt(wxEVT_TOGGLEBUTTON);
        evt.SetEventObject(this);
        ProcessEvent(evt);
    }

    toggle_mode_ = to_enable;
}

bool Button::IsToggleModeEnabled() const noexcept
{
    return toggle_mode_;
}

bool Button::IsPushed() const noexcept
{
    return pushed_;
}

bool Button::IsBeingPushed() const noexcept
{
    return being_pushed_;
}

void Button::paintButton (wxDC &dc,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown
                          )
{
    wxGraphicsContext *gc = wxGraphicsContext::CreateFromUnknownDC(dc);
    HWM_SCOPE_EXIT([gc] { delete gc; });
    assert(gc);
    if(!gc) { return; }

    auto rc = GetClientRect();

    assert(rc.GetX() == 0 && rc.GetY() == 0);

    ButtonTheme::ColourSetting const *c = &theme_.normal_;
    if(shouldDrawButtonAsDown) {
        c = &theme_.down_;
    } else if(shouldDrawButtonAsHighlighted) {
        c = &theme_.highlighted_;
    }

    double const round = theme_.sz_round_;
    auto const edge = theme_.sz_edge_;

    auto const tr = wxTransparentColour;

    BrushPen bp { tr };
    if(shouldDrawButtonAsDown == false || true) {
        // draw background edge
        bp = BrushPen { HSVToColour(0.0f, 0.0f, c->br_background_edge_, 0.6), tr };
        bp.ApplyTo(gc);
        auto tmp = rc;
        gc->DrawRoundedRectangle(0, 0, rc.GetWidth(), rc.GetHeight(), round);
    }

    if(1) {
        // draw background
        bp = BrushPen { HSVToColour(hue_, 0.0, c->br_background_, 0.6), tr };
        bp.ApplyTo(gc);
        gc->DrawRoundedRectangle(0, 0, rc.GetWidth(), rc.GetHeight() - edge, round);
    }

    // draw front edge
    bp = BrushPen { HSVToColour(hue_, c->sa_front_, c->br_front_edge_), tr };
    bp.ApplyTo(gc);
    gc->DrawRoundedRectangle(edge, edge, rc.GetWidth() - (edge * 2), rc.GetHeight() - (edge * 3), round);

    // draw front
    auto const col_top = HSVToColour(hue_, c->sa_front_, c->br_front_grad_top_);
    auto const col_bottom = HSVToColour(hue_, c->sa_front_, c->br_front_grad_bottom_);

    auto brush = gc->CreateLinearGradientBrush(0, 0, 0, rc.GetHeight(), col_top, col_bottom);
    gc->SetBrush(brush);
    gc->DrawRoundedRectangle(edge, edge * 2, rc.GetWidth() - (edge * 2), rc.GetHeight() - (edge * 4), round);

    // draw text highlight
    auto col_text_highlight = HSVToColour(hue_, 0.0, c->br_text_edge_);
    gc->SetFont(dc.GetFont(), col_text_highlight);
    auto rc_text_highlight = rc;
    rc_text_highlight.Deflate(0, 1);
    rc_text_highlight.Translate(0, -1);
    wxDouble tw = 0, th = 0;
    auto const &label = GetLabel();
    gc->GetTextExtent(label, &tw, &th);
    auto center = rc.GetCenter();
    wxDouble tx = center.x - tw / 2.0;
    wxDouble ty = center.y - th / 2.0 - 1;
    gc->DrawText(label, tx, ty);

    // draw text
    auto col_text = HSVToColour(hue_, 0.0, c->br_text_);
    gc->SetFont(dc.GetFont(), col_text);

    ty += 1;
    gc->DrawText(label, tx, ty);

}

void Button::OnLeftDown(MouseEvent &ev)
{
    hwm::dout << __PRETTY_FUNCTION__ << std::endl;
    being_pushed_ = true;

    Refresh();
}

void Button::OnLeftUp(MouseEvent &ev)
{
    hwm::dout << __PRETTY_FUNCTION__ << std::endl;

    if(being_pushed_ == false) {
        return;
    }

    being_pushed_ = false;

    if(IsToggleModeEnabled()) {
        if(GetClientRect().Contain(ev.pt_)) {
            pushed_ = !pushed_;

            wxCommandEvent evt(wxEVT_TOGGLEBUTTON);
            evt.SetEventObject(this);
            ProcessEvent(evt);
        }

    } else {
        wxCommandEvent evt(wxEVT_BUTTON);
        evt.SetEventObject(this);
        ProcessEvent(evt);
    }

    Refresh();
}

void Button::OnLeftDoubleClick(MouseEvent &ev)
{
    hwm::dout << __PRETTY_FUNCTION__ << std::endl;

    being_pushed_ = false;

    if(IsToggleModeEnabled()) {
        if(GetClientRect().Contain(ev.pt_)) {
            pushed_ = !pushed_;

            wxCommandEvent evt(wxEVT_TOGGLEBUTTON);
            evt.SetEventObject(this);
            ProcessEvent(evt);
        }

    } else {
        wxCommandEvent evt(wxEVT_BUTTON);
        evt.SetEventObject(this);
        ProcessEvent(evt);
    }

    Refresh();
}

void Button::OnMouseEnter(MouseEvent &ev)
{
    hover_ = true;
    Refresh();
}

void Button::OnMouseLeave(MouseEvent &ev)
{
    hover_ = false;
    Refresh();
}

void Button::OnMouseMove(MouseEvent &ev)
{
    auto new_hover_state = GetClientRect().Contain(ev.pt_);
    if(new_hover_state != hover_) {
        hover_ = new_hover_state;
        Refresh();
    }
}

void Button::OnPaint(wxDC &dc)
{
    paintButton(dc, hover_, pushed_ || (being_pushed_ && hover_));
}

void Button::OnMouseCaptureLost(MouseCaptureLostEvent &ev)
{
    being_pushed_ = false;
    Refresh();
}

NS_HWM_END
