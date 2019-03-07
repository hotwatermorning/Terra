#pragma once

NS_HWM_BEGIN

class IPianoRollViewStatus
{
protected:
    IPianoRollViewStatus();
    
public:
    static constexpr Int32 kDefaultKeyHeight = 9;
    static constexpr Int32 kDefaultPPQWidth = 48;
    static constexpr Int32 kNumKeys = 128;
    
    virtual
    ~IPianoRollViewStatus();
    
    virtual
    Int32 GetScrollPosition(wxOrientation ort) const = 0;
    
    virtual
    void SetScrollPosition(wxOrientation ort, Int32 pos) = 0;
    
    //! Get zoom factor.
    /*! @return the zoom factor for the orientation.
     *  a value greater then 1.0 means zoom-in, less then 1.0 means zoom-out.
     *  the value always greater than 0.0.
     */
    virtual
    float GetZoomFactor(wxOrientation ort) const = 0;
    
    virtual
    void SetZoomFactor(wxOrientation ort, float factor) = 0;
    
    struct NoteHeight
    {
        float top_;
        float bottom_;
    };
    //! get bottom y position for the note considering y-zoom factor and y-scroll position.
    NoteHeight GetNoteYRange(Int32 note_number) const;
    
    Int32 GetNoteNumber(float y_position);
    float GetTotalNoteHeight() const;
    Int32 GetTpqn() const;
    float GetNoteXPosition(Tick tick) const;
    Tick GetTick(float x_position) const;
};

class IPianoRollWindowComponent
:   public wxWindow
{
protected:
    IPianoRollWindowComponent(wxWindow *parent, IPianoRollViewStatus *view_status,
                              wxWindowID id = wxID_ANY, wxPoint pos = wxDefaultPosition, wxSize size = wxDefaultSize);
    
public:
    IPianoRollViewStatus * GetViewStatus() const;
    
    virtual
    void OnScrolled(wxOrientation ort) {}
    
    virtual
    void OnChangeZoom(wxOrientation ort) {}
    
private:
    IPianoRollViewStatus *view_status_ = nullptr;
};

wxWindow * CreatePianoRollWindow(wxWindow *parent);

NS_HWM_END
