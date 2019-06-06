#include "Keyboard.hpp"

#include <array>

#include "../App.hpp"
#include "../resource/ResourceHelper.hpp"
#include "./Util.hpp"
#include "./Controls.hpp"

NS_HWM_BEGIN

class Keyboard
:   public wxScrolled<wxWindow>
{
public:
    using PlayingNoteList = std::bitset<128>;
    
    static
    wxImage LoadImage(String filename)
    {
        return GetResourceAs<wxImage>({L"keyboard", filename});
    }
    
    Keyboard(wxWindow *parent)
    :   wxScrolled<wxWindow>(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL|wxALWAYS_SHOW_SB)
    {
        wxSize min_size(1, kWhiteKeyHeight);
        
        SetSize(min_size);
        SetMinClientSize(min_size);
        SetMaxClientSize(wxSize(kFullKeysWidth, kWhiteKeyHeight));
        SetVirtualSize(wxSize(kFullKeysWidth, kWhiteKeyHeight));
        
        SetScrollbars(1,
                      kWhiteKeyHeight,
                      kFullKeysWidth,
                      1,
                      kFullKeysWidth / 2,
                      0);
        
        ShowScrollbars(wxSHOW_SB_ALWAYS, wxSHOW_SB_DEFAULT);
        
        auto rect = GetClientRect();
        last_size_ = rect.GetSize();
        
        playing_notes_.reset();
        
        img_white_          = LoadImage(L"pianokey_white.png");
        img_white_pushed_   = LoadImage(L"pianokey_white_pushed.png");
        img_white_pushed_contiguous_   = LoadImage(L"pianokey_white_pushed_contiguous.png");
        img_black_          = LoadImage(L"pianokey_black.png");
        img_black_pushed_   = LoadImage(L"pianokey_black_pushed.png");
        
        img_white_.Rescale(kKeyWidth, kWhiteKeyHeight);
        img_white_pushed_.Rescale(kKeyWidth, kWhiteKeyHeight);
        img_white_pushed_contiguous_.Rescale(kKeyWidth, kWhiteKeyHeight);
        img_black_.Rescale(kKeyWidth+1, kBlackKeyHeight);
        img_black_pushed_.Rescale(kKeyWidth+1, kBlackKeyHeight);
        
        timer_.Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
        timer_.Start(50);
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_DCLICK, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMotion(ev); });
        Bind(wxEVT_MOVE, [this](auto &ev) { Refresh(); });
        Bind(wxEVT_SIZE, [this](auto &ev) { Refresh(); });
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(ev); });
        Bind(wxEVT_SIZE, [this](auto &ev) { OnSize(ev); });
    }
    
    ~Keyboard()
    {}
    
    bool AcceptsFocus() const override { return false; }
    
    static constexpr int kKeyWidth = 16;
    static constexpr int kBlackKeyDispOffset = 6;
    static constexpr int kBlackKeyDispWidth = 11;
    static constexpr int kNumKeys = 128;
    static constexpr int kNumWhiteKeys = 75;
    static constexpr int kFullKeysWidth = kKeyWidth * kNumWhiteKeys;
    static constexpr int kWhiteKeyHeight = 100;
    static constexpr int kBlackKeyHeight = 60;
    
    static wxSize const kWhiteKey;
    static wxSize const kBlackKey;
    
    static wxColor const kPlayingNoteColor;
    
    template<class List>
    static
    bool IsFound(Int32 n, List const &list) {
        return std::find(list.begin(), list.end(), n) != list.end();
    }
    
    static std::vector<Int32> kWhiteKeyIndices;
    static std::vector<Int32> kBlackKeyIndices;
    
    static
    bool IsWhiteKey(Int32 note_number) { return IsFound(note_number % 12, kWhiteKeyIndices); }
    
    static
    bool IsBlackKey(Int32 note_number) { return IsFound(note_number % 12, kBlackKeyIndices); }
    
    BrushPen const col_background { wxColour(0x26, 0x1E, 0x00) };
    
    void OnPaint(wxPaintEvent &ev)
    {
        wxPaintDC dc(this);
        DoPrepareDC(dc);
        
        auto rect = GetClientRect();
        
        col_background.ApplyTo(dc);
        dc.DrawRectangle(rect);
        
        int left, right, dummy;
        CalcUnscrolledPosition(rect.GetLeft(), 0, &left, &dummy);
        CalcUnscrolledPosition(rect.GetRight(), 0, &right, &dummy);
        
        auto draw_key = [&](auto note_num, auto const &prop, auto const &img) {
            int const octave = note_num / 12;
            auto key_rect = prop.rect_;
            key_rect.Offset(octave * kKeyWidth * 7, 0);
            
            if(key_rect.GetLeft() >= right) { return; }
            if(key_rect.GetRight() < left) { return; }
            
            dc.DrawBitmap(wxBitmap(img), key_rect.GetTopLeft());
        };
        
        for(int i = 0; i < kNumKeys; ++i) {
            if(IsWhiteKey(i) == false) { continue; }
            
            bool const is_playing = playing_notes_[i];
            bool next_pushed = false;
            if(i < kNumKeys - 2) {
                if(IsWhiteKey(i+1) && playing_notes_[i+1]) { next_pushed = true; }
                else if(IsWhiteKey(i+2) && playing_notes_[i+2]) { next_pushed = true; }
            }
            
            auto const &img
            = (is_playing && next_pushed)
            ? img_white_pushed_contiguous_
            : (is_playing ? img_white_pushed_ : img_white_);
            
            draw_key(i, kKeyPropertyList[i % 12], img);
        }
        
        for(int i = 0; i < kNumKeys; ++i) {
            if(IsBlackKey(i) == false) { continue; }
            
            bool const is_playing = playing_notes_[i];
            
            auto const &img = (is_playing ? img_black_pushed_ : img_black_);
            draw_key(i, kKeyPropertyList[i % 12], img);
        }
        
        auto font = wxFont(wxFontInfo(wxSize(8, 10)).Family(wxFONTFAMILY_TELETYPE).AntiAliased());
        dc.SetFont(font);
        for(int i = 0; i < kNumKeys; i += 12) {
            int const octave = i / 12;
            auto rc = wxRect(wxPoint(octave * kKeyWidth * 7, rect.GetHeight() * 0.8),
                             wxSize(kKeyWidth, 10));
            
            dc.DrawLabel(wxString::Format("C%d", i / 12 - 2), wxBitmap(), rc, wxALIGN_CENTER);
        }
    }
    
    void OnLeftDown(wxMouseEvent const &ev)
    {
#if _DEBUG
        if(last_dragging_note_ != std::nullopt) {
            hwm::dout << "left down is sent twice unexpectedly." << std::endl;
        }
#endif
        OnMotion(ev);
    }
    
    void OnLeftUp(wxMouseEvent const &ev)
    {
        if(last_dragging_note_) {
            SendNoteOff(*last_dragging_note_);
        }
        
        last_dragging_note_ = std::nullopt;
    }
    
    void OnMotion(wxMouseEvent const &ev)
    {
        if(ev.LeftIsDown() == false) { return; }
        
        auto pt = ev.GetPosition();
        
        auto note = PointToNoteNumber(pt);
        if(note == last_dragging_note_) { return; }
        
        if(last_dragging_note_) {
            SendNoteOff(*last_dragging_note_);
        }
        
        if(note) {
            SendNoteOn(*note);
        }
        
        last_dragging_note_ = note;
    }
    
    wxSize last_size_;
    void OnSize(wxSizeEvent &ev)
    {
        auto center = GetScrollPos(wxHORIZONTAL) + last_size_.GetWidth() / 2;
        
        auto const new_size = ev.GetSize();
        
        SetScrollbars(1, kWhiteKeyHeight, kFullKeysWidth, 1, center - new_size.GetWidth() / 2, 0);
        last_size_ = new_size;
    }
    
    std::optional<int> PointToNoteNumber(wxPoint pt)
    {
        int x, y;
        CalcUnscrolledPosition(pt.x, pt.y, &x, &y);
        
        int const kOctaveWidth = 7.0 * kKeyWidth;
        
        int octave = (int)(x / (double)kOctaveWidth);
        int x_in_oct = x - (kOctaveWidth * octave);
        
        std::optional<int> found;
        
        for(auto index: kBlackKeyIndices) {
            auto key = kKeyPropertyList[index];
            auto rc = key.rect_;
            rc.SetWidth(kBlackKeyDispWidth);
            rc.Inflate(1, 1);
            if(rc.Contains(x_in_oct - kBlackKeyDispOffset, y)) {
                if(!found) { found = (index + octave * 12); }
                break;
            }
        }
        
        for(auto index: kWhiteKeyIndices) {
            auto key = kKeyPropertyList[index];
            auto rc = key.rect_;
            rc.Inflate(1, 1);
            if(rc.Contains(x_in_oct, y)) {
                if(!found) { found = (index + octave * 12); }
                break;
            }
        }
        
        if(found && 0 <= *found && *found < 128) {
            return *found;
        } else {
            return std::nullopt;
        }
    }
    
    void OnTimer()
    {
        auto pj = Project::GetCurrentProject();
        
        std::vector<Project::PlayingNoteInfo> list_seq = pj->GetPlayingSequenceNotes();
        std::vector<Project::PlayingNoteInfo> list_sample = pj->GetPlayingSampleNotes();
        
        PlayingNoteList tmp = {};
        for(auto &list: {list_seq, list_sample}) {
            for(auto &note: list) {
                tmp[note.pitch_] = true;
            }
        }
        
        if(tmp != playing_notes_) {
            playing_notes_ = tmp;
            Refresh();
        }
    }
    
private:
    void SendNoteOn(UInt8 note_number)
    {
        assert(note_number < 128);
        
        auto proj = Project::GetCurrentProject();
        proj->SendSampleNoteOn(sample_note_channel_, note_number);
    }
    
    void SendNoteOff(UInt8 note_number)
    {
        assert(note_number < 128);
        
        auto proj = Project::GetCurrentProject();
        proj->SendSampleNoteOff(sample_note_channel_, note_number);
    }
    
private:
    std::optional<int> last_dragging_note_;
    wxTimer timer_;
    PlayingNoteList playing_notes_;
    wxImage img_white_;
    wxImage img_white_pushed_;
    wxImage img_white_pushed_contiguous_;
    wxImage img_black_;
    wxImage img_black_pushed_;
    int sample_note_channel_ = 0;
    
    struct KeyProperty {
        KeyProperty(int x, wxSize sz)
        :   rect_(wxPoint(x, 0), sz)
        {}
        
        wxRect rect_;
        wxColour color_;
    };
    
    std::vector<KeyProperty> const kKeyPropertyList {
        { kKeyWidth * 0,            kWhiteKey },
        { int(kKeyWidth * 0.5 - 4), kBlackKey },
        { kKeyWidth * 1,            kWhiteKey },
        { int(kKeyWidth * 1.5 - 2), kBlackKey },
        { kKeyWidth * 2,            kWhiteKey },
        { kKeyWidth * 3,            kWhiteKey },
        { int(kKeyWidth * 3.5 - 4), kBlackKey },
        { kKeyWidth * 4,            kWhiteKey },
        { int(kKeyWidth * 4.5 - 3), kBlackKey },
        { kKeyWidth * 5,            kWhiteKey },
        { int(kKeyWidth * 5.5 - 2), kBlackKey },
        { kKeyWidth * 6,            kWhiteKey },
    };
};

wxSize const Keyboard::kWhiteKey { kKeyWidth, kWhiteKeyHeight };
wxSize const Keyboard::kBlackKey { kKeyWidth, kBlackKeyHeight };

wxColor const Keyboard::kPlayingNoteColor { 0x99, 0xEA, 0xFF };

std::vector<Int32> Keyboard::kWhiteKeyIndices = { 0, 2, 4, 5, 7, 9, 11 };
std::vector<Int32> Keyboard::kBlackKeyIndices = { 1, 3, 6, 8, 10 };

wxWindow * CreateVirtualKeyboard(wxWindow *parent)
{
    return new Keyboard(parent);
}

NS_HWM_END
