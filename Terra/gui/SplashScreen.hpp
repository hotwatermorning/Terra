#pragma once

NS_HWM_BEGIN

class ISplashScreen
:   public wxFrame
{
protected:
	template<class... Args>
	ISplashScreen(Args&&... args);

public:
	virtual
    ~ISplashScreen() {}
    
	virtual
    void AddMessage(String msg) = 0;
};

//! Create the splash screen.
//! Invoke Destroy() member function to close it.
ISplashScreen * CreateSplashScreen(wxImage const &img);

NS_HWM_END
