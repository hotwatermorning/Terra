#include "SplashScreen.hpp"
#include "Controls.hpp"
#include <deque>
#include <chrono>
#include <mutex>

#include "Util.hpp"
#include "../misc/MathUtil.hpp"

NS_HWM_BEGIN

template<class... Args>
ISplashScreen::ISplashScreen(Args&&... args)
:	wxFrame(std::forward<Args>(args)...)
{
}

static wxColour const kColMessage = HSVToColour(0.0, 0.0, 0.2);

class SplashScreen
:	public ISplashScreen
{
	class Panel;
    using clock_t = std::chrono::steady_clock;

public:
    SplashScreen(wxImage image)
    :   ISplashScreen()
    ,   image_(image)
    {
#if defined(_MSC_VER)
        Create(nullptr, wxID_ANY, kAppName, wxDefaultPosition, wxDefaultSize,
            wxFRAME_NO_TASKBAR | wxBORDER_NONE);
        SetWindowLong(GetHWND(), GWL_EXSTYLE, GetWindowLong(GetHWND(), GWL_EXSTYLE) | WS_EX_LAYERED);
        wxSize font_size(12, 12);
        font_ = wxFont(wxFontInfo(font_size).Family(wxFONTFAMILY_TELETYPE).AntiAliased(true));
#else
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
        Create(nullptr, wxID_ANY, kAppName, wxDefaultPosition, wxDefaultSize,
            wxFRAME_NO_TASKBAR | wxBORDER_NONE);
        wxSize font_size(15, 15);
        font_ = wxFont(wxFontInfo(font_size).Family(wxFONTFAMILY_MODERN).FaceName("Geneva"));
#endif

        SetSize(image_.GetWidth(), image_.GetHeight());
        CentreOnScreen();

        back_buffer_ = GraphicsBuffer(GetSize());

        Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) { StartClosingWithEffect(); });
        Bind(wxEVT_PAINT, [this](auto &ev) { OnPaint(); });

        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](auto &ev) { OnTimer(); });

        Show(true);
        SetFocus();
    }

    ~SplashScreen()
    {
    }

    void StartClosingWithEffect()
    {
        last_time_ = clock_t::now();
        timer_.Start(16);
    }

    void AddMessage(String msg) override
    {
        auto lock = std::unique_lock<std::mutex>(mtx_);
        if (messages_.size() > 6) { messages_.pop_front(); }
        messages_.push_back(msg);

        lock.unlock();

        Refresh();
    }

    bool HasTransparentBackground() override
    {
        return true;
    }

    void OnPaint()
    {
        Render();

        int const k = std::min<int>(255, 256 - 256 * (opaque_));

        wxMemoryDC memory_dc(back_buffer_.GetBitmap());
        assert(memory_dc.IsOk());
        
#if defined(_MSC_VER)
        auto hwnd = GetHWND();
        wxClientDC cdc(this);
        POINT pt_src{ 0, 0 };
        POINT pt_dest{ 0, 0 };
        BLENDFUNCTION bf;
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = 255 - k;
        bf.AlphaFormat = AC_SRC_ALPHA;
        SIZE size{ GetClientSize().GetWidth(), GetClientSize().GetHeight() };

        auto src_hdc = memory_dc.GetHDC();
        auto dest_hdc = cdc.GetHDC();

        UpdateLayeredWindow(hwnd, dest_hdc, nullptr, &size, src_hdc, &pt_src, 0, &bf, ULW_ALPHA);
#else
        wxPaintDC pdc(this);
        pdc.Blit(wxPoint(), GetClientSize(), &memory_dc, wxPoint());
        SetTransparent(255 - k);
#endif
    }

    void Render()
    {
        back_buffer_.Clear();
        
        wxMemoryDC memory_dc(back_buffer_.GetBitmap());
        assert(memory_dc.IsOk());
        
        wxGCDC dc(memory_dc);
        
        auto img = wxBitmap(image_, 32);
        dc.DrawBitmap(img, wxPoint());
        
        dc.SetFont(font_);
        dc.SetTextForeground(kColMessage);

        auto lock = std::unique_lock<std::mutex>(mtx_);
        static int count = 0;
        for (int i = 0; i < messages_.size(); ++i) {
            auto rect = GetClientRect();
            rect.SetTopLeft(rect.GetBottomRight() / 2);
            rect.SetSize(rect.GetSize() / 2);
            rect.Deflate(20, 20);
            rect.SetHeight(font_.GetPixelSize().GetHeight());
            rect.Offset(0, i * rect.GetHeight());

            auto const &str = messages_[i];

#if defined(_MSC_VER)
            auto gdip = (Gdiplus::Graphics *)(dc.GetGraphicsContext()->GetNativeContext());
            gdip->SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
#endif
            dc.DrawLabel(str, rect, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
        }
        lock.unlock();
    }

    void OnTimer()
    {
        auto now = clock_t::now();
        auto elapsed = std::chrono::duration<double>(now - last_time_).count();
        opaque_ = Clamp<double>(opaque_ - elapsed / 2.0, 0, 1.0);
        last_time_ = now;

        if (opaque_ > 0) {
            Refresh(true);
        }
        else {
            Destroy();
        }
    }

private:
    GraphicsBuffer back_buffer_;
    wxImage image_;
    wxTimer timer_;
    wxFont font_;
    std::mutex mtx_;
    std::deque<String> messages_;
    clock_t::time_point last_time_;
    double opaque_ = 1.0;
    wxColour col_msg_;
};

//====================================================================================================

ISplashScreen * CreateSplashScreen(wxImage const &img)
{
	return new SplashScreen(img);
}

NS_HWM_END
