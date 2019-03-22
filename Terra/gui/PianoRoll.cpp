#include "PianoRoll.hpp"
#include "Controls.hpp"
#include "Util.hpp"
#include "Keyboard.hpp"
#include "../misc/MathUtil.hpp"
#include "../project/Sequence.hpp"
#include "../project/Project.hpp"
#include "../App.hpp"

NS_HWM_BEGIN

IPianoRollViewStatus::ZoomFactorRange IPianoRollViewStatus::kZoomRangeHorz = { 0.5, 5 };
IPianoRollViewStatus::ZoomFactorRange IPianoRollViewStatus::kZoomRangeVert = { 0.8, 5 };

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

float IPianoRollViewStatus::GetTotalHeight() const
{
    float const yzoom = GetZoomFactor(wxVERTICAL);
    return kNumKeys * kDefaultKeyHeight * yzoom;
}

float IPianoRollViewStatus::GetTotalWidth() const
{
    float const xzoom = GetZoomFactor(wxHORIZONTAL);
    return GetTotalTick() / (double)GetTpqn() * kDefaultPPQWidth * xzoom;
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
    
    return (Tick)((x_position + xscroll) / xzoom / kDefaultPPQWidth * GetTpqn());
}

IPianoRollWindowComponent::IPianoRollWindowComponent(wxWindow *parent, IPianoRollViewStatus *view_status,
                                                     wxWindowID id,  wxPoint pos, wxSize size)
:   IRenderableWindow<wxWindow>(parent, id, pos, size)
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
:   public IPianoRollWindowComponent
,   public MyApp::ChangeProjectListener
{
public:
    Sequence sequence_;
    
    PianoRollEditor(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IPianoRollWindowComponent(parent, view_status)
    {
        slr_change_project_.reset(MyApp::GetInstance()->GetChangeProjectListeners(), this);
        OnChangeCurrentProject(nullptr, Project::GetCurrentProject());
        
        Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &ev) { OnMouseWheel(ev); });
        timer_.Bind(wxEVT_TIMER, [this](wxTimerEvent &ev) { OnTimer(); });
        timer_.Start(30);
    }
    
    void OnMouseWheel(wxMouseEvent &ev)
    {
        //hwm::dout << "Axis: {}"_format(ev.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL ? "Vertical" : "horizontal") << std::endl;
        
        auto view_status = GetViewStatus();
        
        if(ev.ShiftDown() && ev.ControlDown()) {
            // 時間軸
            auto range = view_status->kZoomRangeHorz;
            auto cur = view_status->GetZoomFactor(wxHORIZONTAL);
            cur *= (100 - ev.GetWheelRotation()) / 100.0;
            cur = Clamp<double>(cur, range.min_, range.max_);
            view_status->SetZoomFactor(wxHORIZONTAL, cur, ev.GetPosition().x);
        } else if(ev.ControlDown()) {
            // ピッチ
            auto range = view_status->kZoomRangeVert;
            auto cur = view_status->GetZoomFactor(wxVERTICAL);
            cur *= (100 - ev.GetWheelRotation()) / 100.0;
            cur = Clamp<double>(cur, range.min_, range.max_);
            view_status->SetZoomFactor(wxVERTICAL, cur, ev.GetPosition().y);
        } else if(ev.ShiftDown() || ev.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL) {
            auto pos = view_status->GetScrollPosition(wxHORIZONTAL);
            pos += ev.GetWheelRotation();
            auto size = GetClientSize();
            pos = Clamp<Int32>(pos, 0, std::max<int>(view_status->GetTotalWidth(), size.GetWidth()) - size.GetWidth());
            view_status->SetScrollPosition(wxHORIZONTAL, pos);
        } else {
            auto pos = view_status->GetScrollPosition(wxVERTICAL);
            pos -= ev.GetWheelRotation();
            auto size = GetClientSize();
            pos = Clamp<Int32>(pos, 0, std::max<int>(view_status->GetTotalHeight(), size.GetHeight()) - size.GetHeight());
            view_status->SetScrollPosition(wxVERTICAL, pos);
        }
    }
    
    BrushPen col_white_key = { HSVToColour(0.0, 0.0, 0.8) };
    BrushPen col_black_key = { HSVToColour(0.0, 0.0, 0.64) };
    BrushPen col_white_key_gap = { HSVToColour(0.0, 0.0, 0.64) };
    BrushPen col_note = { HSVToColour(0.25, 0.22, 1.0), HSVToColour(0.0, 0.0, 0.6) };
    
    BrushPen col_beat = { HSVToColour(0.0, 0.0, 0.6, 0.15)};
    BrushPen col_measure = { HSVToColour(0.0, 0.0, 0.4, 0.4) };
    BrushPen col_transpor_bar = { HSVToColour(0.15, 0.7, 1.0, 0.7) };
    
    void doRender(wxDC &dc) override
    {
        auto const size = GetClientSize();
        
        dc.SetClippingRegion(wxPoint{}, size);
        
        col_white_key.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());
        
        auto const view_status = GetViewStatus();
        
        auto is_white_key = [](int key_number) {
            auto const k = key_number % 12;
            for(auto n: {0, 2, 4, 5, 7, 9, 11}) {
                if(k % 12 == n) { return true; }
            }
            return false;
        };
        
        // draw key lines
        for(int kn = 0; kn < view_status->kNumKeys; ++kn) {
            auto const yrange = view_status->GetNoteYRange(kn);
            
            if(yrange.top_ >= size.y) { continue; }
            if(yrange.bottom_ <= 0) { break; }
            
            //if(is_white_key(kn))    { col_white_key.ApplyTo(dc); }
            //else                    { col_black_key.ApplyTo(dc); }
            
            Int32 top = (Int32)std::round(yrange.top_);
            Int32 bottom = (Int32)std::round(yrange.bottom_);
            if(is_white_key(kn) == false) {
                col_black_key.ApplyTo(dc);
                dc.DrawRectangle(0, top, size.GetWidth(), bottom - top);
            }
            
            if((kn % 12) == 0 || (kn % 12) == 5) {
                col_white_key_gap.ApplyTo(dc);
                dc.DrawLine(0, bottom, size.GetWidth(), bottom);
            }
        }
        
        // draw measure bars
        auto const beat = (Tick)480;
        auto const meas = beat * 4;
        auto left_tick = view_status->GetTick(0);
        auto right_tick = view_status->GetTick(size.GetWidth());
        
        for(int t = left_tick; t < right_tick; t += beat) {
            auto aligned = (int)((t + beat - 1) / beat) * beat;
            if(aligned % meas == 0) {
                col_measure.ApplyTo(dc);
            } else {
                col_beat.ApplyTo(dc);
            }
            
            Int32 x = (Int32)std::round(view_status->GetNoteXPosition(aligned));
            dc.DrawLine(x, 0, x, size.GetHeight());
        }
        
        for(auto &note: sequence_.notes_) {
            auto rect = GetRectFromNote(note);
            Int32 top = (Int32)std::round(rect.m_y);
            Int32 bottom = (Int32)std::round(rect.GetBottom());
            Int32 left = (Int32)std::round(rect.m_x);
            Int32 right = (Int32)std::round(rect.GetRight());
            
            wxRect rc(wxPoint{left, top}, wxPoint{right, bottom});
            col_note.ApplyTo(dc);
            auto min_edge = std::min<double>(rc.GetWidth(), rc.GetHeight());
            auto round = Clamp<double>(min_edge / 5, 1, 6);
            dc.DrawRoundedRectangle(rc, round);
        }
        
        // draw transport bar
        auto const pj = Project::GetCurrentProject();
        auto const &tp = pj->GetTransporter();
        auto const state = tp.GetCurrentState();
        auto const tp_x = (Int32)std::round(view_status->GetNoteXPosition(state.play_.begin_.tick_));
        if(0 <= tp_x && tp_x <= size.GetWidth()) {
            col_transpor_bar.ApplyTo(dc);
            dc.DrawLine(tp_x, 0, tp_x, size.GetHeight());
        }
//        BrushPen bp_circle { HSVToColour(0.3, 0.3, 0.8, 0.2) };
//        bp_circle.ApplyTo(dc);
//        dc.DrawCircle(100, 100, 100);
        
        dc.DestroyClippingRegion();
    }
    
    Int32 GetNoteIndexFromPoint(wxPoint pt)
    {
        for(Int32 i = 0; i < sequence_.notes_.size(); ++i) {
            auto &note = sequence_.notes_[i];
            auto rc = GetRectFromNote(note);
            if(rc.Contains(pt)) {
                return i;
            }
        }
        
        return -1;
    }

    wxRect2DDouble GetRectFromNote(Sequence::Note note) const
    {
        auto view_status = GetViewStatus();
        auto left = view_status->GetNoteXPosition(note.pos_);
        auto right = view_status->GetNoteXPosition(note.GetEndPos());
        auto yrange = view_status->GetNoteYRange(note.pitch_);
        
        return wxRect2DDouble(left, yrange.top_, right - left, yrange.GetHeight());
    }
    
    wxRect2DDouble GetRectFromNote(Int32 note_index) const
    {
        assert(note_index < sequence_.notes_.size());
        return GetRectFromNote(sequence_.notes_[note_index]);
    }
    
    void OnTimer()
    {
        auto const pj = Project::GetCurrentProject();
        auto const &tp = pj->GetTransporter();
        auto const state = tp.GetCurrentState();
        
        auto const new_tick = state.play_.begin_.tick_;
        if(new_tick != last_pos_) {
            last_pos_ = new_tick;
            Refresh();
        }
    }
    
private:
    wxTimer timer_;
    SampleCount last_pos_ = 0;
    ScopedListenerRegister<MyApp::ChangeProjectListener> slr_change_project_;
    
    void OnChangeCurrentProject(Project *old_pj, Project *new_pj) override
    {
        if(new_pj && new_pj->GetNumSequences() >= 1) {
            sequence_ = new_pj->GetSequence(0);
        } else {
            sequence_ = Sequence();
        }
    }
};

class PianoRollKeyboard
:   public IPianoRollWindowComponent
{
public:
    PianoRollKeyboard(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IPianoRollWindowComponent(parent, view_status)
    {}

    BrushPen col_white_key = { HSVToColour(0.0, 0.0, 0.98) };
    BrushPen col_black_key = { HSVToColour(0.0, 0.0, 0.4) };
    BrushPen col_white_key_gap = { HSVToColour(0.0, 0.0, 0.73) };
    double kBlackKeyWidthRatio = 0.7;
    std::array<double, 12> kBlackKeyLineHeightTable = {
        { 0.0, 0.6, 0.0, 0.4, 0.0, 0.0, 0.7, 0.0, 0.5, 0.0, 0.3, 0.0 }
    };

    void doRender(wxDC &dc) override
    {
        auto const size = GetSize();

        dc.SetClippingRegion(wxPoint{}, size);
        
        col_white_key.ApplyTo(dc);
        dc.DrawRectangle(GetClientRect());

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

            Int32 top = (Int32)std::round(yrange.top_);
            Int32 bottom = (Int32)std::round(yrange.bottom_);
            
            if(is_white_key(kn) == false) {
                col_white_key_gap.ApplyTo(dc);
                auto line_height = std::round((bottom - top) * (1 - kBlackKeyLineHeightTable[kn % 12]) + top);
                dc.DrawLine(0, line_height, size.GetWidth(), line_height);
                col_black_key.ApplyTo(dc);
                dc.DrawRectangle(0, top, size.GetWidth() * kBlackKeyWidthRatio, bottom - top);
            }
            
            if(kn % 12 == 0 || kn % 12 == 5) {
                col_white_key_gap.ApplyTo(dc);
                dc.DrawLine(0, bottom, size.GetWidth(), bottom);
            }
        }

        dc.DestroyClippingRegion();
    }
};

class PianoRollEnvelopeHeader
:   public IPianoRollWindowComponent
{
public:
    PianoRollEnvelopeHeader(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IPianoRollWindowComponent(parent, view_status)
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
:   public IPianoRollWindowComponent
{
public:
    PianoRollEnvelope(wxWindow *parent, IPianoRollViewStatus *view_status)
    :   IPianoRollWindowComponent(parent, view_status)
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
    constexpr static Int32 kHorizontalSashWidth = 12;
    constexpr static Int32 kVerticalSashWidth = 2;
    constexpr static Int32 kSBWidth = 12; // scroll bar width
    using base_window_type = wxWindow;
    
    PianoRollWindow(wxWindow *parent, wxWindowID id = wxID_ANY,
                    wxPoint pos = wxDefaultPosition, wxSize size = wxDefaultSize)
    :   base_window_type(parent, id, pos, size)
    ,   IPianoRollViewStatus()
    {
		SetDoubleBuffered(true);
        keyboard_ = new PianoRollKeyboard(this, this);
        editor_ = new PianoRollEditor(this, this);
        envelope_header_ = new PianoRollEnvelopeHeader(this, this);
        envelope_ = new PianoRollEnvelope(this, this);
        
        sb_vert_ = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVERTICAL);
        sb_horz_ = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHORIZONTAL);
        
        auto bind_scroll_bar_events = [](wxScrollBar *sb, auto func) {
            for(auto ev: {
                wxEVT_SCROLL_PAGEUP, wxEVT_SCROLL_PAGEDOWN,
                wxEVT_SCROLL_LINEUP, wxEVT_SCROLL_LINEDOWN,
                wxEVT_SCROLL_TOP, wxEVT_SCROLL_BOTTOM,
                wxEVT_SCROLL_THUMBTRACK
            }) { sb->Bind(ev, func); }
        };
        
        bind_scroll_bar_events(sb_horz_, [this](auto &ev) { OnScrollHorz(ev); });
        bind_scroll_bar_events(sb_vert_, [this](auto &ev) { OnScrollVert(ev); });
        
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

		wxGCDC dc(memory_dc);
        Render(dc);
        
        wxPaintDC paint_dc(this);
        paint_dc.Blit(wxPoint{0, 0}, GetClientSize(), &memory_dc, wxPoint {0, 0});
    }
    
    void Render(wxDC &dc)
    {
        dc.SetBackground(wxBrush(HSVToColour(0.2, 0.8, 0.9, 0.7)));
        dc.Clear();
        
        BrushPen bgr(HSVToColour(0.0, 0.0, 0.2));
        bgr.ApplyTo(dc);
        
        dc.DrawRectangle(GetClientRect());
        
        BrushPen bp_sash {
            HSVToColour(0.7, 0.4, 0.4),
            HSVToColour(0.7, 0.1, 0.7),
        };
        
        bp_sash.ApplyTo(dc);
        
        // paint sash
        auto const rc_sash_horz = GetHorizontalSashRect();
        auto const rc_sash_vert = GetVerticalSashRect();
        
        dc.DrawRectangle(rc_sash_vert);
        dc.DrawRectangle(rc_sash_horz);
        
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
        auto const y_min = size.GetHeight() - horz_sash_y_remaining_max - kHorizontalSashWidth;
        auto const y_max = size.GetHeight() - horz_sash_y_remaining_min - kHorizontalSashWidth;
        horz_sash_y = Clamp<int>(horz_sash_y, y_min, y_max);
        
        wxRect rc;
        
        int const x0 = 0;
        int const x1 = vert_sash_x;
        int const x2 = x1 + kVerticalSashWidth;
        int const x3 = size.GetWidth() - kSBWidth;
        int const x4 = size.GetWidth();
        
        int const y0 = 0;
        int const y1 = horz_sash_y;
        int const y2 = y1 + kHorizontalSashWidth;
        int const y3 = size.GetHeight() - kSBWidth;
        int const y4 = size.GetHeight();
        
        keyboard_->SetSize          (wxRect{wxPoint{x0, y0}, wxSize{x1-x0, y1-y0}});
        editor_->SetSize            (wxRect{wxPoint{x2, y0}, wxSize{x3-x2, y1-y0}});
        envelope_header_->SetSize   (wxRect{wxPoint{x0, y2}, wxSize{x1-x0, y3-y2}});
        envelope_->SetSize          (wxRect{wxPoint{x2, y2}, wxSize{x3-x2, y3-y2}});
        
        sb_horz_->SetSize(wxRect{wxPoint{x2, y3}, wxSize{x3 - x2, y4 - y3}});
        sb_vert_->SetSize(wxRect{wxPoint{x3, y0}, wxSize{x4 - x3, y1 - y0}});
        
        LayoutScroll(wxHORIZONTAL, GetScrollPosition(wxHORIZONTAL));
        LayoutScroll(wxVERTICAL, GetScrollPosition(wxVERTICAL));

        return base_window_type::Layout();
    }
    
    //! @return pos is adjusted.
    bool LayoutScrollBarImpl(wxScrollBar *sb, int window_size, int total, int page, int new_thumb_position)
    {
        window_size = std::min<int>(window_size, total);
        assert(new_thumb_position >= 0);
        auto const pos = std::min<int>(new_thumb_position, total - window_size);
        sb->SetScrollbar(pos, window_size, total, page);
        
        return pos != new_thumb_position;
    }
    
    void LayoutScroll(wxOrientation ort, int new_thumb_position)
    {
        if(ort == wxHORIZONTAL) {
            auto window = editor_->GetClientSize().GetWidth();
            LayoutScrollBarImpl(sb_horz_, window, GetTotalWidth(), window / 2, new_thumb_position);
        } else {
            auto window = editor_->GetClientSize().GetHeight();
            LayoutScrollBarImpl(sb_vert_, window, GetTotalHeight(), window / 2, new_thumb_position);
        }
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

        // moving vertical sash is not supported.
//        if(rc_sash_vert.Contains(ev.GetPosition())) {
//            move_mode_ = MoveMode::kVertical;
//            delta_x_ = ev.GetPosition().x - vert_sash_x;
//        } else
        if(rc_sash_horz.Contains(ev.GetPosition())) {
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
            wxSize(kVerticalSashWidth, size.GetHeight())
        };
    }
    
    wxRect GetHorizontalSashRect() const
    {
        auto const size = GetClientSize();
        return wxRect {
            wxPoint(0, horz_sash_y),
            wxSize(size.GetWidth(), kHorizontalSashWidth)
        };
    }
    
    Int32 GetScrollPosition(wxOrientation ort) const override
    {
        return (ort == wxHORIZONTAL ? sb_horz_ : sb_vert_)->GetThumbPosition();
    }
    
    void SetScrollPosition(wxOrientation ort, Int32 pos) override
    {
        assert(pos >= 0);
        auto &sb = (ort == wxHORIZONTAL ? sb_horz_ : sb_vert_);
        
        sb->SetThumbPosition(std::min<Int32>(pos, sb->GetRange()));
        Refresh();
    }
    
    float GetZoomFactor(wxOrientation ort) const override
    {
        return (ort == wxHORIZONTAL ? zoom_x_ : zoom_y_);
    }
    
    void SetZoomFactor(wxOrientation ort, float factor, int zooming_pos) override
    {        
        auto apply = [this](wxOrientation ort,
                            float new_zoom,
                            int zooming_pos,
                            float &zoom,
                            wxScrollBar *sb)
        {
            if(new_zoom == zoom) { return; }
            
            auto a = sb->GetThumbPosition() / zoom;
            auto b = (zooming_pos + sb->GetThumbPosition()) / zoom;
            auto w = b - a;
            auto ratio = new_zoom / zoom;
            auto c = b - w / ratio;
            zoom = new_zoom;
            auto new_scroll_pos = std::max<Int32>(0, std::round(c * zoom));
            LayoutScroll(ort, new_scroll_pos);
        };
        
        assert(factor > 0);
        
        apply(ort, factor, zooming_pos,
              (ort == wxHORIZONTAL ? zoom_x_ : zoom_y_),
              (ort == wxHORIZONTAL ? sb_horz_ : sb_vert_));
        Refresh();
    }
    
    Tick GetTotalTick() const override
    {
        return 1920 * 10;
    }
    
    void OnScrollHorz(wxScrollEvent &)
    {
        Refresh();
    }
    
    void OnScrollVert(wxScrollEvent &)
    {
        Refresh();
    }
    
private:
    IPianoRollWindowComponent *keyboard_;
    IPianoRollWindowComponent *editor_;
    IPianoRollWindowComponent *envelope_header_;
    IPianoRollWindowComponent *envelope_;
    static constexpr int horz_sash_y_remaining_min = 100;
    static constexpr int horz_sash_y_remaining_max = 200;
    static constexpr int vert_sash_x_min = 60;
    static constexpr int vert_sash_x_max = 60;
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
    float zoom_x_ = 1.0;
    float zoom_y_ = 1.0;
    wxScrollBar *sb_vert_;
    wxScrollBar *sb_horz_;
};

wxWindow * CreatePianoRollWindow(wxWindow *parent)
{
    return new PianoRollWindow(parent);
}

NS_HWM_END
