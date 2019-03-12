#include "SplashScreen.hpp"
#include <deque>
#include <chrono>
#include <mutex>

NS_HWM_BEGIN

class SplashScreen::Panel
:   public wxPanel
{
public:
    using clock_t = std::chrono::steady_clock;
    
    Panel(wxWindow *parent, wxImage image)
    :   wxPanel(parent, wxID_ANY)
    ,   image_(image)
    {
        if(image_.HasAlpha() == false) {
            image_.SetAlpha();
        }
        
        wxSize font_size(15, 15);

#if defined(_MSC_VER)
		font_ = wxFont(wxFontInfo(font_size).Family(wxFONTFAMILY_MODERN).FaceName("Tahoma"));
#else
		font_ = wxFont(wxFontInfo(font_size).Family(wxFONTFAMILY_MODERN).FaceName("Geneva"));
#endif
        col_msg_ = *wxBLACK;
        SetForegroundColour(col_msg_);
        
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });
        
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });
    }
    
    ~Panel()
    {
    }
    
    void OnPaint()
    {
        wxPaintDC dc(this);
        auto bitmap = wxBitmap(image_);
        dc.DrawBitmap(bitmap, 0, 0);
        
        auto lock = std::unique_lock<std::mutex>(mtx_);
        for(int i = 0; i < messages_.size(); ++i) {
            auto rect = GetClientRect();
            rect.SetTopLeft(rect.GetBottomRight() / 2);
            rect.SetSize(rect.GetSize() / 2);
            rect.Deflate(20, 20);
            rect.SetHeight(font_.GetPixelSize().GetHeight());
            rect.Offset(0, i * rect.GetHeight());
            SetForegroundColour(col_msg_);
            dc.SetFont(font_);
            dc.DrawLabel(messages_[i], rect, wxALIGN_RIGHT|wxALIGN_CENTER_VERTICAL);
        }
    }
    
    void StartClosingWithEffect()
    {
        last_time_ = clock_t::now();
        timer_.Start(16);
    }
    
    void OnTimer()
    {
        auto new_time = clock_t::now();
        auto duration = std::chrono::duration<double>(new_time - last_time_);
        last_time_ = new_time;

        auto alpha_data = image_.GetAlpha();
        int const w = image_.GetWidth();
        int const h = image_.GetHeight();

        double const amount = std::max<unsigned long>(1, std::round(duration.count() * 256));

        bool found_opaque = false;

        for(int i = 0; i < w * h; ++i) {
            alpha_data[i] -= std::min<unsigned long>(alpha_data[i], amount);
            found_opaque = found_opaque || (alpha_data[i] != 0);
        }
        
        auto const rgba = col_msg_.GetRGBA();
        auto alpha = rgba >> 24;
        alpha -= std::min<unsigned long>(alpha, amount);
        col_msg_.SetRGBA((rgba & 0xFFFFFF) | (alpha << 24));

        if(found_opaque) {
            Refresh();
        } else {
            timer_.Stop();
            GetParent()->Destroy();
        }
    }
    
    void AddMessage(String msg)
    {
        auto lock = std::unique_lock<std::mutex>(mtx_);
        if(messages_.size() > 6) { messages_.pop_front(); }
        messages_.push_back(msg);
        
        lock.unlock();
        CallAfter([this] { Refresh(); });
    }
    
private:
    wxImage image_;
    wxTimer timer_;
    wxFont font_;
    std::mutex mtx_;
    std::deque<String> messages_;
    clock_t::time_point last_time_;
    wxColour col_msg_;
};



SplashScreen::SplashScreen(wxImage image)
:   wxFrame()
{
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    
    Create(nullptr, wxID_ANY, kAppName, wxDefaultPosition, wxDefaultSize,
           wxFRAME_NO_TASKBAR);
    
    SetSize(image.GetWidth(), image.GetHeight());
    CentreOnScreen();
    
    panel_ = new Panel(this, image);
    panel_->SetSize(GetSize());
    panel_->Show();
    
    Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) {
        panel_->StartClosingWithEffect();
    });
    
    Layout();
    Refresh();
}

SplashScreen::~SplashScreen()
{
    assert(panel_ == nullptr);
}

bool SplashScreen::Destroy()
{
    RemoveChild(panel_);
    panel_->Destroy();
    panel_ = nullptr;
    return wxFrame::Destroy();
}

void SplashScreen::AddMessage(String msg)
{
    panel_->AddMessage(msg);
}

NS_HWM_END
