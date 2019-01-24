#include "Keyboard.hpp"

#include <array>
#include <wx/stdpaths.h>

#include "../App.hpp"

NS_HWM_BEGIN

class Keyboard
:   public wxPanel
{
public:
    using PlayingNoteList = std::array<bool, 128>;
    
    static
    wxImage LoadImage(String filename)
    {
        return wxStandardPaths::Get().GetResourcesDir() + L"/keyboard/" + filename;
    }
    
    Keyboard(wxWindow *parent)
    :   wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(0, kWhiteKeyHeight))
    {
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
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        
        timer_.Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
        timer_.Start(50);
        Bind(wxEVT_LEFT_DOWN, [this](auto &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](auto &ev) { OnLeftUp(ev); });
        Bind(wxEVT_MOTION, [this](auto &ev) { OnMotion(ev); });
        Bind(wxEVT_KEY_DOWN, [this](auto &ev) { OnKeyDown(ev); });
        Bind(wxEVT_KEY_UP, [this](auto &ev) { OnKeyUp(ev); });
        
        key_code_for_sample_note_.fill(0);
    }
    
    ~Keyboard()
    {}
    
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
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        
        auto rect = GetClientRect();
        
        dc.SetPen(wxPen(wxColor(0x26, 0x1E, 0x00)));
        dc.SetBrush(wxBrush(wxColor(0x26, 0x1E, 0x00)));
        dc.DrawRectangle(rect);
        
        int const disp_half = rect.GetWidth() / 2;
        int const disp_shift = kFullKeysWidth / 2 - disp_half;
        
        auto draw_key = [&](auto note_num, auto const &prop, auto const &img) {
            int const octave = note_num / 12;
            auto key_rect = prop.rect_;
            key_rect.Offset(octave * kKeyWidth * 7 - disp_shift, 0);
            
            if(key_rect.GetLeft() >= rect.GetWidth()) { return; }
            if(key_rect.GetRight() < 0) { return; }
            
            dc.DrawBitmap(wxBitmap(img), key_rect.GetTopLeft());
            
            //            if(is_playing) {
            //                col_pen = kKeyBorderColorPlaying;
            //                col_brush = kPlayingNoteColor;
            //                dc.SetPen(wxPen(col_pen));
            //                dc.SetBrush(wxBrush(col_brush));
            //            }
            //            dc.DrawRoundedRectangle(key_rect, 2);
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
        
        auto font = wxFont(wxFontInfo(wxSize(8, 10)).Family(wxFONTFAMILY_DEFAULT));
        dc.SetFont(font);
        for(int i = 0; i < kNumKeys; i += 12) {
            int const octave = i / 12;
            dc.DrawLabel(wxString::Format("C%d", i / 12 - 2),
                         wxBitmap(),
                         wxRect(wxPoint(octave * kKeyWidth * 7 - disp_shift, rect.GetHeight() * 0.8),
                                wxSize(kKeyWidth, 10)),
                         wxALIGN_CENTER
                         );
        }
    }
    
    void OnLeftDown(wxMouseEvent const &ev)
    {
        assert(last_dragging_note_ == std::nullopt);
        OnMotion(ev);
    }
    
    void OnLeftUp(wxMouseEvent const &ev)
    {
        if(last_dragging_note_) {
            SendSampleNoteOff(*last_dragging_note_);
        }
        
        last_dragging_note_ = std::nullopt;
    }
    
    void OnMotion(wxMouseEvent const &ev)
    {
        if(ev.LeftIsDown() == false) { return; }
        
        auto pt = ev.GetPosition();
        auto note = PointToNoteNumber(pt);
        
        if(last_dragging_note_ && last_dragging_note_ != note) {
            SendSampleNoteOff(*last_dragging_note_);
        }
        
        if(note) {
            SendSampleNoteOn(*note);
        }
        
        last_dragging_note_ = note;
    }
    
    void OnKeyDown(wxKeyEvent const &ev)
    {
        if(ev.HasAnyModifiers()) { return; }
        
        auto uc = ev.GetUnicodeKey();
        if(uc == WXK_NONE ) { return; }
        
        if(uc == kOctaveUp) {
            if(key_base_ + 12 < 128) { key_base_ += 12; }
            return;
        } else if(uc == kOctaveDown) {
            if(key_base_ - 12 >= 0) { key_base_ -= 12; }
            return;
        }
        
        auto found = std::find(kKeyTable.begin(), kKeyTable.end(), uc);
        if(found == kKeyTable.end()) { return; }
        int note_number = key_base_ + (found - kKeyTable.begin());
        if(note_number >= 128) { return; }
        
        key_code_for_sample_note_[note_number] = uc;
        SendSampleNoteOn(note_number);
    }
    
    void OnKeyUp(wxKeyEvent const &ev)
    {
        auto uc = ev.GetUnicodeKey();
        if(uc == WXK_NONE ) { return; }
        
        if(uc == kPlayback) {
            auto pj = Project::GetCurrentProject();
            auto &tp = pj->GetTransporter();
            tp.SetPlaying(!tp.IsPlaying());
        }
        
        for(int i = 0; i < key_code_for_sample_note_.size(); ++i) {
            if(key_code_for_sample_note_[i] == uc) {
                SendSampleNoteOff(i);
                key_code_for_sample_note_[i] = 0;
            }
        }
    }
    
    std::optional<int> PointToNoteNumber(wxPoint pt)
    {
        auto rect = GetClientRect();
        int const disp_half = rect.GetWidth() / 2;
        int const disp_shift = kFullKeysWidth / 2 - disp_half;
        
        double const x = (pt.x + disp_shift);
        int const kOctaveWidth = 7.0 * kKeyWidth;
        
        int octave = (int)(x / (double)kOctaveWidth);
        int x_in_oct = x - (kOctaveWidth * octave);
        
        std::optional<int> found;
        
        for(auto index: kBlackKeyIndices) {
            auto key = kKeyPropertyList[index];
            auto rc = key.rect_;
            rc.SetWidth(kBlackKeyDispWidth);
            rc.Inflate(1, 1);
            if(rc.Contains(x_in_oct - kBlackKeyDispOffset, pt.y)) {
                if(!found) { found = (index + octave * 12); }
                break;
            }
        }
        
        for(auto index: kWhiteKeyIndices) {
            auto key = kKeyPropertyList[index];
            auto rc = key.rect_;
            rc.Inflate(1, 1);
            if(rc.Contains(x_in_oct, pt.y)) {
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
    void SendSampleNoteOn(UInt8 note_number)
    {
        assert(note_number < 128);
        
        auto proj = Project::GetCurrentProject();
        proj->SendSampleNoteOn(sample_note_channel, note_number);
    }
    
    void SendSampleNoteOff(UInt8 note_number)
    {
        assert(note_number < 128);
        
        auto proj = Project::GetCurrentProject();
        proj->SendSampleNoteOff(sample_note_channel, note_number);
    }
    
    void SendSampleNoteOffForAllKeyDown()
    {
        for(int i = 0; i < key_code_for_sample_note_.size(); ++i) {
            if(key_code_for_sample_note_[i] != 0) {
                SendSampleNoteOff(i);
            }
        }
        key_code_for_sample_note_.fill(0);
    }
    
private:
    //std::optional<wxPoint> _;
    std::optional<int> last_dragging_note_;
    std::array<wxChar, 128> key_code_for_sample_note_;
    wxTimer timer_;
    PlayingNoteList playing_notes_;
    int key_base_ = 60;
    constexpr static wxChar kPlayback = L' ';
    constexpr static wxChar kOctaveDown = L'Z';
    constexpr static wxChar kOctaveUp = L'X';
    static std::vector<wxChar> const kKeyTable;
    int sample_note_channel = 0;
    wxImage img_white_;
    wxImage img_white_pushed_;
    wxImage img_white_pushed_contiguous_;
    wxImage img_black_;
    wxImage img_black_pushed_;
    
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

std::vector<wxChar> const Keyboard::kKeyTable = {
    L'A', L'W', L'S', L'E', L'D', L'F', L'T', L'G', L'Y', L'H', L'U', L'J', L'K', L'O', L'L', L'P'
};

wxSize const Keyboard::kWhiteKey { kKeyWidth, kWhiteKeyHeight };
wxSize const Keyboard::kBlackKey { kKeyWidth, kBlackKeyHeight };

wxColor const Keyboard::kPlayingNoteColor { 0x99, 0xEA, 0xFF };

std::vector<Int32> Keyboard::kWhiteKeyIndices = { 0, 2, 4, 5, 7, 9, 11 };
std::vector<Int32> Keyboard::kBlackKeyIndices = { 1, 3, 6, 8, 10 };

wxPanel * CreateVirtualKeyboard(wxWindow *parent)
{
    return new Keyboard(parent);
}

NS_HWM_END
