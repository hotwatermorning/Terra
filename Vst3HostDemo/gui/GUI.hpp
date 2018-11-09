#pragma once

#include <memory>
#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

class MyPanel;

class MyFrame
:   public wxFrame
,   SingleInstance<MyFrame>
{
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    ~MyFrame();
private:
    bool Destroy() override;
    void OnExit();
    void OnAbout(wxCommandEvent& event);
    void OnPlay(wxCommandEvent& event);
    void OnTimer();
    
private:
    std::string msg_;
    wxTimer timer_;
    MyPanel *my_panel_;
};

NS_HWM_END
