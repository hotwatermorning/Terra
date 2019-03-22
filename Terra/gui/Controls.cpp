#include "Controls.hpp"
#include "Util.hpp"

NS_HWM_BEGIN

void IRenderableWindowBase::Render(wxDC &dc)
{
    if(is_shown_ == false) { return; }
    doRender(dc);
}

ImageButton::ImageButton(wxWindow *parent,
                         bool is_3state,
                         wxImage normal,
                         wxImage hover,
                         wxImage pushed,
                         wxImage hover_pushed,
                         wxPoint pos,
                         wxSize size)
:   IRenderableWindow<>(parent, wxID_ANY, pos, size)
,   orig_normal_(normal)
,   orig_hover_(hover)
,   orig_pushed_(pushed)
,   orig_hover_pushed_(hover_pushed)
,   is_3state_(is_3state)
{
    SetMinSize(normal.GetSize());
    SetSize(normal.GetSize());
    Bind(wxEVT_ENTER_WINDOW, [this](auto &ev) {
        is_hover_ = true;
        Refresh();
    });
    
    Bind(wxEVT_LEAVE_WINDOW, [this](auto &ev) {
        is_hover_ = false;
        is_being_pressed_ = false;
        Refresh();
    });
    
    Bind(wxEVT_LEFT_DOWN, [this](auto &ev) {
        is_being_pressed_ = true;
        Refresh();
    });
    
    Bind(wxEVT_LEFT_DCLICK, [this](auto &ev) {
        is_being_pressed_ = true;
        Refresh();
    });
    
    Bind(wxEVT_LEFT_UP, [this](auto &ev) {
        if(!is_hover_) { return; }
        if(is_3state_) {
            is_pushed_ = !is_pushed_;
        } else {
            is_pushed_ = false;
        }
        is_being_pressed_ = false;
        
        wxEventType type;
        if(is_3state_) {
            type = wxEVT_TOGGLEBUTTON;
        } else {
            type = wxEVT_BUTTON;
        }
        wxCommandEvent new_ev(type, GetId());
        ProcessWindowEvent(new_ev);
        Refresh();
    });
    
    SetAutoLayout(true);
    Layout();
}

bool ImageButton::IsPushed() const { return is_pushed_; }
void ImageButton::SetPushed(bool status) { is_pushed_ = status; }

bool ImageButton::Layout()
{
    auto size = GetClientSize();
    normal_ = orig_normal_.Scale(size.x, size.y);
    hover_ = orig_hover_.Scale(size.x, size.y);
    pushed_ = orig_pushed_.Scale(size.x, size.y);
    hover_pushed_ = orig_hover_pushed_.Scale(size.x, size.y);
    
    return IRenderableWindow<>::Layout();
}

void ImageButton::doRender(wxDC &dc)
{
    if(IsShown() == false) { return; }
    
    wxImage img;
    if(is_being_pressed_) {
        img = pushed_;
    } else if(is_hover_) {
        if(is_pushed_) {
            img = hover_pushed_;
        } else {
            img = hover_;
        }
    } else if(is_pushed_) {
        img = pushed_;
    } else {
        img = normal_;
    }
    
    dc.DrawBitmap(img, 0, 0);
}

//------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------

Label::Label(wxWindow *parent)
:   IRenderableWindow<>(parent, wxID_ANY)
{
    font_ = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font_.SetPixelSize(wxSize(12, 12));

    col_ = HSVToColour(0.0, 0.0, 0.2, 1.0);

    // treat mouse event for this window as one for the parent event.
    auto mouse_callback = [this](wxMouseEvent &ev) {
        auto const parent = GetParent();
        if(parent == nullptr) { return; }
        
        auto pt = ClientToScreen(ev.GetPosition());
        ev.SetPosition(parent->ScreenToClient(pt));
        ev.ResumePropagation(1);
        ev.Skip();
    };
    
    auto generic_callback = [](wxEvent &ev) {
        ev.ResumePropagation(1);
        ev.Skip();
    };

    Bind(wxEVT_LEFT_DOWN,       mouse_callback);
    Bind(wxEVT_LEFT_UP,         mouse_callback);
    Bind(wxEVT_LEFT_DCLICK,     mouse_callback);
    Bind(wxEVT_RIGHT_DOWN,      mouse_callback);
    Bind(wxEVT_RIGHT_UP,        mouse_callback);
    Bind(wxEVT_RIGHT_DCLICK,    mouse_callback);
    Bind(wxEVT_MIDDLE_DOWN,     mouse_callback);
    Bind(wxEVT_MIDDLE_UP,       mouse_callback);
    Bind(wxEVT_MIDDLE_DCLICK,   mouse_callback);
    Bind(wxEVT_MOTION,          mouse_callback);
    Bind(wxEVT_SET_FOCUS,       generic_callback);
}

bool Label::AcceptsFocus() const { return false; }

void Label::doRender(wxDC &dc)
{
    dc.SetTextForeground(col_);
    dc.SetFont(font_);

#if defined(_MSC_VER)
    {
        auto gdip = (Gdiplus::Graphics *)(dc.GetGraphicsContext()->GetNativeContext());
        gdip->SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    }
#endif

    dc.DrawLabel(text_, GetClientRect(), align_);
}

void Label::SetText(wxString new_text)
{
    text_ = new_text;
    Refresh();
}

wxString Label::GetText() const
{
    return text_;
}

void Label::SetAlignment(int align)
{
    align_ = align;
}

void Label::SetFont(wxFont font)
{
    font_ = font;
}

wxFont Label::GetFont() const
{
    return font_;
}

void Label::SetTextColour(wxColour col)
{
    col_ = col;
}

wxColour Label::GetTextColour() const
{
    return col_;
}

int Label::GetAlignment() const
{
    return align_;
}

NS_HWM_END
