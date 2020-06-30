//
//  Button.cpp
//  Terra
//
//  Created by yuasa on 2020/06/27.
//

#include "Button.hpp"
#include "Util.hpp"
#include <wx/graphics.h>

NS_HWM_BEGIN

Button::ButtonTheme Button::getDefaultButtonTheme() noexcept
{
    return {
        3.0f, 1.0f,
        { 0.5f, 0.4f, 0.1f, 0.78f, 0.72f, 0.66f, 0.15f, 0.9f },
        { 0.5f, 0.4f, 0.1f, 0.88f, 0.82f, 0.76f, 0.15f, 0.9f },
        { 0.5f, 0.4f, 0.1f, 0.32f, 0.54f, 0.48f, 0.15f, 0.9f },
    };
}

Button::Button(wxWindow *parent,
               wxWindowID id,
               float hue,
               ButtonTheme theme,
               wxPoint pos,
               wxSize size)
:   wxWindow(parent, id, pos, size)
,   theme_(theme)
{
    setHue(hue);

    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &ev) { OnLeftDown(ev); });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &ev) { OnLeftUp(ev); });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &ev) { OnMouseEnter(ev); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &ev) { OnMouseLeave(ev); });
    Bind(wxEVT_PAINT, [this](wxPaintEvent &) { OnPaint(); });
    Bind(wxEVT_MOTION, [this](wxMouseEvent &ev) { OnMouseMove(ev); });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &ev) { OnCaptureLost(ev); });
    Bind(wxEVT_MOUSE_CAPTURE_CHANGED, [this](wxMouseCaptureChangedEvent &ev) { std::cout << "Capture Changed." << std::endl; });
}

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

        wxCommandEvent evt(wxEVT_TOGGLEBUTTON, GetId());
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
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
    auto rc = GetClientRect();

    ButtonTheme::ColourSetting const *c = &theme_.normal_;
    if(shouldDrawButtonAsDown) {
        c = &theme_.down_;
    } else if(shouldDrawButtonAsHighlighted) {
        c = &theme_.highlighted_;
    }

    auto const round = theme_.sz_round_;
    auto const edge = theme_.sz_edge_;

    BrushPen bp { HSVToColour(0.0f, 0.0f, c->br_background_edge_) };
    bp.ApplyTo(dc);
    dc.DrawRoundedRectangle(rc, round);

    bp = BrushPen { HSVToColour(hue_, 0.0, c->br_background_) };
    bp.ApplyTo(dc);
    dc.DrawRoundedRectangle(0, 0, rc.width, rc.height - edge, round);

    bp = BrushPen { HSVToColour(hue_, c->sa_front_, c->br_front_edge_) };
    bp.ApplyTo(dc);
    dc.DrawRoundedRectangle(edge, edge, rc.width - (edge * 2), rc.height - (edge * 3), round);

    auto const col_top = HSVToColour(hue_, c->sa_front_, c->br_front_grad_top_);
    auto const col_bottom = HSVToColour(hue_, c->sa_front_, c->br_front_grad_bottom_);

    wxGraphicsContext *gc = wxGraphicsContext::CreateFromUnknownDC(dc);
    assert(gc);
    if(gc) {
        auto brush = gc->CreateLinearGradientBrush(0, 0, 0, rc.height, col_top, col_bottom);
        gc->SetBrush(brush);
        gc->DrawRoundedRectangle(edge, edge * 2, rc.width - (edge * 2), rc.height - (edge * 4), round);

        delete gc;
    }

    auto col_text_highlight = HSVToColour(hue_, 0.0, c->br_text_edge_);
    dc.SetTextForeground(col_text_highlight);
    auto rc_text_highlight = rc;
    rc_text_highlight.Deflate(0, 1);
    rc_text_highlight.Offset(0, -1);
    dc.DrawLabel(GetLabel(), rc_text_highlight, wxALIGN_CENTER);

    auto col_text = HSVToColour(hue_, 0.0, c->br_text_);
    dc.SetTextForeground(col_text);
    auto rc_text = rc;
    dc.DrawLabel(GetLabel(), rc_text, wxALIGN_CENTER);
}

void Button::OnLeftDown(wxMouseEvent &ev)
{
    being_pushed_ = true;

    Refresh();
}

void Button::OnLeftUp(wxMouseEvent &ev)
{
    if(being_pushed_ == false) {
        return;
    }

    being_pushed_ = false;

    if(IsToggleModeEnabled()) {
        if(GetClientRect().Contains(ev.GetPosition())) {
            pushed_ = !pushed_;

            wxCommandEvent evt(wxEVT_TOGGLEBUTTON, GetId());
            evt.SetEventObject(this);
            ProcessWindowEvent(evt);
        }

    } else {
        wxCommandEvent evt(wxEVT_BUTTON, GetId());
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    }

    Refresh();
}

void Button::OnMouseEnter(wxMouseEvent &ev)
{
    hover_ = true;
    Refresh();
}

void Button::OnMouseLeave(wxMouseEvent &ev)
{
    hover_ = false;
    Refresh();
}

void Button::OnMouseMove(wxMouseEvent &ev)
{
    auto new_hover_state = GetClientRect().Contains(ev.GetPosition());
    if(new_hover_state != hover_) {
        hover_ = new_hover_state;
        Refresh();
    }
}

void Button::OnPaint()
{
    wxPaintDC dc(this);
    paintButton(dc, hover_, pushed_ || (being_pushed_ && hover_));
}

void Button::OnCaptureLost(wxMouseCaptureLostEvent &ev)
{
    being_pushed_ = false;
    Refresh();
}

NS_HWM_END
