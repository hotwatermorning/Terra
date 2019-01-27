#pragma once

NS_HWM_BEGIN

class SplashScreen
:   public wxFrame
{
    class Panel;
    
public:
    SplashScreen(wxImage image);
    ~SplashScreen();
    
    bool Destroy() override;
    void AddMessage(String msg);
    
private:
    Panel *panel_;
};

NS_HWM_END
