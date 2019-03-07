#include "PianoRoll.hpp"
#include "Controls.hpp"
#include "Util.hpp"
#include "../misc/MathUtil.hpp"

#include <wx/splitter.h>

NS_HWM_BEGIN

IPianoRollViewStatus::IPianoRollViewStatus()
{}

IPianoRollViewStatus::~IPianoRollViewStatus()
{}

//! get bottom y position for the note considering y-zoom factor and y-scroll position.
IPianoRollViewStatus::NoteHeight IPianoRollViewStatus::GetNoteYRange(Int32 note_number) const
{
    float const yzoom = GetZoomFactor(wxVERTICAL);
    Int32 const yscroll = GetScrollPosition(wxVERTICAL);
    
    return NoteHeight {
        (kNumKeys - note_number - 1) * kDefaultKeyHeight * yzoom - yscroll,
        (kNumKeys - note_number) * kDefaultKeyHeight * yzoom - yscroll,
    };
}

Int32 IPianoRollViewStatus::GetNoteNumber(float y_position)
{
    if(y_position < 0) { return kNumKeys - 1; }
    float const yzoom = GetZoomFactor(wxVERTICAL);
    Int32 const yscroll = GetScrollPosition(wxVERTICAL);
    
    assert(yzoom > 0);
    
    Int32 virtual_y_pos = (Int32)std::round((y_position - yscroll) / yzoom);
    Int32 tmp_note_number = kNumKeys - (Int32)(virtual_y_pos / kDefaultKeyHeight) - 1;
    
    return std::min<Int32>(kNumKeys - 1, tmp_note_number);
}

float IPianoRollViewStatus::GetTotalNoteHeight() const
{
    float const yzoom = GetZoomFactor(wxVERTICAL);
    return kNumKeys * kDefaultKeyHeight * yzoom;
}

//! todo: get this from the current project.
Int32 IPianoRollViewStatus::GetTpqn() const { return 480; }

float IPianoRollViewStatus::GetNoteXPosition(Tick tick) const
{
    float const xzoom = GetZoomFactor(wxHORIZONTAL);
    Int32 const xscroll = GetScrollPosition(wxHORIZONTAL);
    
    return (float)tick / GetTpqn() * kDefaultPPQWidth * xzoom - xscroll;
}

Tick IPianoRollViewStatus::GetTick(float x_position) const
{
    float const xzoom = GetZoomFactor(wxHORIZONTAL);
    Int32 const xscroll = GetScrollPosition(wxHORIZONTAL);
    
    return (Tick)((x_position - xscroll) / xzoom / kDefaultPPQWidth * GetTpqn());
}

IPianoRollWindowComponent::IPianoRollWindowComponent(wxWindow *parent, IPianoRollViewStatus *view_status,
                                                     wxWindowID id,  wxPoint pos, wxSize size)
:   wxWindow(parent, id, pos, size)
,   view_status_(view_status)
{
    assert(GetParent());
    assert(view_status_);
}

IPianoRollViewStatus * IPianoRollWindowComponent::GetViewStatus() const
{
    return view_status_;
}

class PianoRollEditor
:   public IRenderableWindow<IPianoRollWindowComponent>
{
public:
    PianoRollEditor(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IRenderableWindow<IPianoRollWindowComponent>(parent, view_status)
    {}
    
    BrushPen col_white_key = { HSVToColour(0.3, 0.3, 0.9), HSVToColour(0.3, 0.0, 0.7) };
    BrushPen col_black_key = { HSVToColour(0.3, 0.3, 0.1), HSVToColour(0.3, 0.0, 0.2) };
    
    void doRender(wxDC &dc) override
    {
        auto const size = GetSize();
        
        dc.SetClippingRegion(wxPoint{}, size);
        
        auto const view_status = GetViewStatus();
        
        auto is_white_key = [](int key_number) {
            auto const k = key_number % 12;
            for(auto n: {0, 2, 4, 5, 7, 9, 11}) {
                if(k % 12 == n) { return true; }
            }
            return false;
        };
        
        for(int kn = 0; kn < view_status->kNumKeys; ++kn) {
            auto const yrange = view_status->GetNoteYRange(kn);
            
            if(yrange.top_ >= size.y) { continue; }
            if(yrange.bottom_ <= 0) { break; }
            
            if(is_white_key(kn))    { col_white_key.ApplyTo(dc); }
            else                    { col_black_key.ApplyTo(dc); }
            
            dc.DrawRectangle(0, yrange.top_, size.GetWidth(), yrange.bottom_);
        }
        
        dc.DestroyClippingRegion();
    }
};

class PianoRollKeyboard
:   public IRenderableWindow<IPianoRollWindowComponent>
{
public:
    PianoRollKeyboard(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IRenderableWindow<IPianoRollWindowComponent>(parent, view_status)
    {}
    
    BrushPen col_white_key = { HSVToColour(0.3, 0.3, 0.9), HSVToColour(0.3, 0.0, 0.7) };
    BrushPen col_black_key = { HSVToColour(0.3, 0.3, 0.1), HSVToColour(0.3, 0.0, 0.2) };
    
    void doRender(wxDC &dc) override
    {
        auto const size = GetSize();
        
        dc.SetClippingRegion(wxPoint{}, size);
        
        auto const view_status = GetViewStatus();
        
        auto is_white_key = [](int key_number) {
            auto const k = key_number % 12;
            for(auto n: {0, 2, 4, 5, 7, 9, 11}) {
                if(k % 12 == n) { return true; }
            }
            return false;
        };
        
        for(int kn = 0; kn < view_status->kNumKeys; ++kn) {
            auto const yrange = view_status->GetNoteYRange(kn);
            
            if(yrange.top_ >= size.y) { continue; }
            if(yrange.bottom_ <= 0) { break; }
            
            if(is_white_key(kn))    { col_white_key.ApplyTo(dc); }
            else                    { col_black_key.ApplyTo(dc); }
            
            dc.DrawRectangle(0, yrange.top_, size.GetWidth(), yrange.bottom_);
        }
        
        dc.DestroyClippingRegion();
    }
};

class PianoRollEnvelopeHeader
:   public IRenderableWindow<IPianoRollWindowComponent>
{
public:
    PianoRollEnvelopeHeader(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IRenderableWindow<IPianoRollWindowComponent>(parent, view_status)
    {}
    
    void doRender(wxDC &dc) override
    {
        auto rc = GetClientRect();
        BrushPen col { HSVToColour(0.6, 1.0, 0.9) };
        col.ApplyTo(dc);
        dc.DrawRectangle(rc);
        dc.DrawText("Piano Roll Envelope Header", 0, 0);
    }
};

class PianoRollEnvelope
:   public IRenderableWindow<IPianoRollWindowComponent>
{
public:
    PianoRollEnvelope(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IRenderableWindow<IPianoRollWindowComponent>(parent, view_status)
    {}
    
    void doRender(wxDC &dc) override
    {
        auto rc = GetClientRect();
        BrushPen col { HSVToColour(0.9, 1.0, 0.9) };
        col.ApplyTo(dc);
        dc.DrawRectangle(rc);
        dc.DrawText("Piano Roll Envelope", 0, 0);
    }

};

class PianoRollWindow
:   public wxWindow
,   public IPianoRollViewStatus
{
public:
    UInt32 kSashWidth = 6;
    using base_window_type = wxWindow;
    
    PianoRollWindow(wxWindow *parent, wxWindowID id = wxID_ANY,
                    wxPoint pos = wxDefaultPosition, wxSize size = wxDefaultSize)
    :   base_window_type(parent, id, pos, size)
    ,   IPianoRollViewStatus()
    {
        keyboard_ = new PianoRollKeyboard(this, this);
        editor_ = new PianoRollEditor(this, this);
        envelope_header_ = new PianoRollEnvelopeHeader(this, this);
        envelope_ = new PianoRollEnvelope(this, this);
        
        Bind(wxEVT_PAINT, [this](auto &) { OnPaint(); });
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMouseMove(ev); });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](auto &ev) {
            move_mode_ = MoveMode::kNone;
        });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { OnKeyUp(ev); });
        Bind(wxEVT_SIZE, [this](auto &ev) { Layout(); });
        
        SetAutoLayout(true);
        Layout();
    }
    
    void OnPaint()
    {
        wxMemoryDC memory_dc(bitmap_);
        Render(memory_dc);
        
        wxPaintDC paint_dc(this);
        paint_dc.Blit(wxPoint{0, 0}, GetClientSize(), &memory_dc, wxPoint {0, 0});
    }
    
    void Render(wxDC &dc)
    {
        dc.FloodFill(wxPoint{}, HSVToColour(0, 0, 0, 0));
        
        BrushPen bp_sash {
            HSVToColour(0.7, 0.4, 0.4),
            HSVToColour(0.7, 0.1, 0.7),
        };
        
        bp_sash.ApplyTo(dc);
        
        // paint sash
        auto const rc_sash_horz = GetHorizontalSashRect();
        auto const rc_sash_vert = GetVerticalSashRect();
        
        dc.DrawRectangle(rc_sash_horz);
        dc.DrawRectangle(rc_sash_vert);
        
        keyboard_->SetOriginAndRender(dc);
        editor_->SetOriginAndRender(dc);
        envelope_header_->SetOriginAndRender(dc);
        envelope_->SetOriginAndRender(dc);
    }
    
    bool Layout() override
    {
        wxImage image(GetClientSize());
        image.SetAlpha();
        bitmap_ = image;
        
        auto const size = GetClientSize();
        
        vert_sash_x = Clamp<int>(vert_sash_x, vert_sash_x_min, vert_sash_x_max);
        auto const y_min = size.GetHeight() - horz_sash_y_remaining_max - kSashWidth;
        auto const y_max = size.GetHeight() - horz_sash_y_remaining_min - kSashWidth;
        horz_sash_y = Clamp<int>(horz_sash_y, y_min, y_max);
        
        wxRect rc;
        
        int const x0 = 0;
        int const x1 = vert_sash_x;
        int const x2 = x1 + kSashWidth;
        int const x3 = size.GetWidth();
        
        int const y0 = 0;
        int const y1 = horz_sash_y;
        int const y2 = y1 + kSashWidth;
        int const y3 = size.GetHeight();
        
        keyboard_->SetSize          (wxRect{wxPoint{x0, y0}, wxSize{x1-x0, y1-y0}});
        editor_->SetSize            (wxRect{wxPoint{x2, y0}, wxSize{x3-x2, y1-y0}});
        envelope_header_->SetSize   (wxRect{wxPoint{x0, y2}, wxSize{x1-x0, y3-y2}});
        envelope_->SetSize          (wxRect{wxPoint{x2, y2}, wxSize{x3-x2, y3-y2}});

        return base_window_type::Layout();
    }
    
    void OnKeyUp(wxKeyEvent &ev)
    {
        hwm::dout
        << L"[key up] ukey:{}, rkey:{}, rkeyflag:{}  shift:{}, cmd:{}, ctrl:{}"_format(
            ev.GetUnicodeKey(), ev.GetRawKeyCode(), ev.GetRawKeyFlags(),
            ev.ShiftDown(), ev.CmdDown(), ev.RawControlDown()
        )
        << std::endl;
    }
    
    void OnLeftDown(wxMouseEvent &ev)
    {
        auto const rc_sash_horz = GetHorizontalSashRect();
        auto const rc_sash_vert = GetVerticalSashRect();
        
        delta_x_ = 0;
        delta_y_ = 0;
        
        if(rc_sash_vert.Contains(ev.GetPosition())) {
            move_mode_ = MoveMode::kVertical;
            delta_x_ = ev.GetPosition().x - vert_sash_x;
        } else if(rc_sash_horz.Contains(ev.GetPosition())) {
            move_mode_ = MoveMode::kHorizontal;
            delta_y_ = ev.GetPosition().y - horz_sash_y;
        } else {
            move_mode_ = MoveMode::kNone;
            return;
        }
        
        if(!HasCapture()) {
            CaptureMouse();
        }
        
        Refresh();
    }
    
    void OnLeftUp(wxMouseEvent &ev)
    {
        if(HasCapture()) {
            ReleaseMouse();
            move_mode_ = MoveMode::kNone;
        }
    }
    
    void OnMouseMove(wxMouseEvent &ev)
    {
        if(move_mode_ == MoveMode::kNone) { return; }
        
        if(move_mode_ == MoveMode::kVertical) {
            vert_sash_x = ev.GetPosition().x - delta_x_;
        } else if(move_mode_ == MoveMode::kHorizontal) {
            horz_sash_y = ev.GetPosition().y - delta_y_;
        }
        
        Layout();
        Refresh();
    }
    
    wxRect GetVerticalSashRect() const
    {
        auto const size = GetClientSize();
        return wxRect {
            wxPoint(vert_sash_x, 0),
            wxSize(kSashWidth, size.GetHeight())
        };
    }
    
    wxRect GetHorizontalSashRect() const
    {
        auto const size = GetClientSize();
        return wxRect {
            wxPoint(0, horz_sash_y),
            wxSize(size.GetWidth(), kSashWidth)
        };
    }
    
    Int32 GetScrollPosition(wxOrientation ort) const override
    {
        return (ort == wxHORIZONTAL ? scroll_x_ : scroll_y_);
    }
    
    void SetScrollPosition(wxOrientation ort, Int32 pos) override
    {
        assert(pos >= 0);
        (ort == wxHORIZONTAL ? scroll_x_ : scroll_y_) = pos;
    }
    
    float GetZoomFactor(wxOrientation ort) const override
    {
        return (ort == wxHORIZONTAL ? zoom_x_ : zoom_y_);
    }
    
    void SetZoomFactor(wxOrientation ort, float factor) override
    {
        assert(factor > 0);
        (ort == wxHORIZONTAL ? zoom_x_ : zoom_y_) = factor;
    }
    
private:
    PianoRollKeyboard *keyboard_;
    PianoRollEditor *editor_;
    PianoRollEnvelopeHeader *envelope_header_;
    PianoRollEnvelope *envelope_;
    static constexpr int horz_sash_y_remaining_min = 100;
    static constexpr int horz_sash_y_remaining_max = 200;
    static constexpr int vert_sash_x_min = 100;
    static constexpr int vert_sash_x_max = 300;
    int horz_sash_y = 1000;
    int vert_sash_x = 100;
    wxBitmap bitmap_;
    int delta_x_ = 0; // 垂直サッシ移動時の、マウスポインタとサッシの位置の差
    int delta_y_ = 0; // 水平サッシ移動時の、マウスポインタとサッシの位置の差
    enum MoveMode {
        kNone,
        kVertical,
        kHorizontal,
    };
    MoveMode move_mode_ = kNone;
    Int32 scroll_x_ = 0;
    Int32 scroll_y_ = 0;
    float zoom_x_ = 1.0;
    float zoom_y_ = 1.0;
};

wxWindow * CreatePianoRollWindow(wxWindow *parent)
{
    return new PianoRollWindow(parent);
}

NS_HWM_END
