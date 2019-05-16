#pragma once

#include "../misc/SingleInstance.hpp"
#include <map>
#include <algorithm>

NS_HWM_BEGIN

class PCKeyboardInput
:   public SingleInstance<PCKeyboardInput>
,   public wxEvtHandler
{
    static constexpr Int32 kInvalidPitch = -1;
    
    enum KeyID {
        kID_C = 12340, kID_Db, kID_D, kID_Eb, kID_E,
        kID_F, kID_Gb, kID_G, kID_Ab, kID_A, kID_Bb,
        kID_B, kID_hiC, kID_hiDb, kID_hiD, kID_hiEb,
        kID_OctDown, kID_OctUp, kID_Unknown,
    };
    
    static
    std::map<wchar_t, KeyID> const & get_key_id_map() {
        static std::map<wchar_t, KeyID> const map {
            { L'A', kID_C }, { L'W', kID_Db }, { L'S', kID_D }, { L'E', kID_Eb },
            { L'D', kID_E }, { L'F', kID_F },  { L'T', kID_Gb }, { L'G', kID_G },
            { L'Y', kID_Ab }, { L'H', kID_A }, { L'U', kID_Bb }, { L'J', kID_B },
            { L'K', kID_hiC }, { L'O', kID_hiDb }, { L'L', kID_hiD }, { L'P', kID_hiEb },
            { L'Z', kID_OctDown }, { L'X', kID_OctUp }, { L'\0', kID_Unknown },
        };
        return map;
    }
    
    static
    wchar_t KeyIDToChar(KeyID id)
    {
        auto &map = get_key_id_map();
        auto found = std::find_if(map.begin(), map.end(), [id](auto &elem) {
            return elem.second == id;
        });
        
        return (found == map.end() ? '\0' : found->first);
    }
    
    static
    KeyID CharToKeyID(wchar_t c)
    {
        auto &map = get_key_id_map();
        auto found = map.find(c);
        return (found == map.end() ? kID_Unknown : found->second);
    }
    
public:
    PCKeyboardInput();
    ~PCKeyboardInput();
    
    void ApplyTo(wxFrame *frame);
    
private:
    void OnTimer();
    
    wxTimer timer_;
    
    //! key -> pitch
    std::map<KeyID, int> playing_keys_;
    wxAcceleratorTable acc_table_;
    int base_pitch_ = 48;
    
    wxAcceleratorTable const & GetAcceleratorTable() const
    {
        return acc_table_;
    }
    
    void TransposeOctaveUp();
    void TransposeOctaveDown();
    
    //! Get the note number for the id with the current octave setting.
    Int32 KeyIDToPitch(KeyID id) const;
};

NS_HWM_END
