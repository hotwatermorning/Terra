#pragma once

#include "PianoRoll.hpp"

NS_HWM_BEGIN

class IKeyboard
:   public IPianoRollWindowComponent
{
protected:
    IKeyboard(wxWindow *parent, IPianoRollViewStatus *view_status);
    
public:
    virtual ~IKeyboard();
};

//! create the keyboard window.
/*! @param parent is the parent window.
 *  @param ort is the display orientation.
 *  if wxVERTICAL is pecified, each key of the keyboard ordered vertically.
 */
IKeyboard * CreateVirtualKeyboard(wxWindow *parent, IPianoRollViewStatus *view_status, wxOrientation ort);

NS_HWM_END
