#include "TestDialog.hpp"
#include "Controls.hpp"

NS_HWM_BEGIN

class ChildWidget
:   public IWidget
{
public:
    ChildWidget()
    {
        static int i;
        shift_y = i * 10;
        ++i;

        btn1_.setHue(0.0f);
        btn2_.setHue(0.33f);
        btn3_.setHue(0.67f);

        btn1_.SetLabel(L"red");
        btn2_.SetLabel(L"green");
        btn3_.SetLabel(L"blue");

        FSize sz_button = { 100, 50 };
        btn1_.SetSize(sz_button);
        btn2_.SetSize(sz_button);
        btn3_.SetSize(sz_button);

        AddChild(&btn1_);
        AddChild(&btn2_);
        AddChild(&btn3_);

        btn1_.Bind(wxEVT_BUTTON, [this](wxCommandEvent &ev) {
            assert(ev.GetEventObject() == &btn1_);
            std::cout << "Button1 pushed." << std::endl;
        });

        btn2_.EnableToggleMode(true);
        btn2_.Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &ev) {
            assert(ev.GetEventObject() == &btn2_);
            std::string state = btn2_.IsPushed() ? "Down" : "Up";
            std::cout << ("Button2 is " + state) << std::endl;
        });
    }

    void OnPaint(wxDC &dc) override
    {
        BrushPen bp { HSVToColour(hue_, 0.2f, 0.9f) };
        bp.ApplyTo(dc);
        dc.DrawRoundedRectangle(wxRect(GetClientRect().Translated(0, shift_y).Deflated(0, shift_y)), 10);
    }

    void OnSetPosition() override
    {}

    void OnSetSize() override
    {
        auto rc = GetClientRect().Deflated(50);

        btn1_.SetPosition(rc.GetPosition().Translated(0, 80 * 0));
        btn2_.SetPosition(rc.GetPosition().Translated(0, 80 * 1));
        btn3_.SetPosition(rc.GetPosition().Translated(0, 80 * 2));

        Refresh();
    }

    void SetHue(float hue) { hue_ = hue; }

private:
    float hue_;
    int shift_y = 0;
    Button btn1_;
    Button btn2_;
    Button btn3_;
};

class TestWidget
:   public IWidget
{
public:
    TestWidget()
    {
        AddChild(&c1_);
        AddChild(&c2_);
        AddChild(&c3_);

        c1_.SetHue(0.1);
        c2_.SetHue(0.8);
        c3_.SetHue(0.5);
    }

    void OnPaint(wxDC &dc) override
    {
        BrushPen bp { HSVToColour(0.0f, 0.0f, 0.0) };
        bp.ApplyTo(dc);
        dc.DrawRectangle(wxRect(GetClientRect()));
        dc.SetBrush(wxBrush(HSVToColour(0.3, 0.8, 0.9)));
        dc.DrawRoundedRectangle(wxRect(GetClientRect()), 5);
    }

    void OnLeftDown(MouseEvent &ev) override
    {
        Refresh();
    }

    void OnSetSize() override
    {
        auto rc = GetClientRect().Deflated(50);

        auto left = rc.WithSize(rc.GetSize().Scaled(0.333, 1)).Deflated(10);
        auto center = rc.WithSize(rc.GetSize().Scaled(0.333, 1)).Translated(rc.GetWidth() * 1.0 / 3.0, 0).Deflated(10);
        auto right = rc.WithSize(rc.GetSize().Scaled(0.333, 1)).Translated(rc.GetWidth() * 2.0 / 3.0, 0).Deflated(10);

        c1_.SetPosition(left.pos);
        c1_.SetSize(left.size);
        c2_.SetPosition(center.pos);
        c2_.SetSize(center.size);
        c3_.SetPosition(right.pos);
        c3_.SetSize(right.size);

        Refresh();
    }

    void OnSetPosition() override
    {}

private:
    ChildWidget c1_;
    ChildWidget c2_;
    ChildWidget c3_;
};

class TestFrame
:   public wxDialog
{
    wxRect const kRect = { wxPoint(0, 0), wxSize(500, 600) };

public:
    TestFrame(wxWindow *parent)
    :   wxDialog(parent, wxID_ANY, "Test", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER|wxSTAY_ON_TOP)
    {
        widget_owner_ = new WidgetOwnerWindow(this, std::make_unique<TestWidget>());

#if defined(_MSC_VER)
        SetDoubleBuffered(true);
#endif

        SetMinClientSize(kRect.GetSize());
        SetMaxClientSize(wxSize { kRect.GetWidth() * 2, kRect.GetHeight() * 2});
        SetClientSize(kRect.GetSize());
        SetAutoLayout(true);

        Bind(wxEVT_CLOSE_WINDOW, [this](auto &ev) {
            Destroy();
        });

        Layout();
        Show(true);
    }

    ~TestFrame() {
    }

    bool Layout() override
    {
        widget_owner_->SetSize(GetClientSize());
        return true;
    }

private:
    wxWindow *active_panel_ = nullptr;
    WidgetOwnerWindow *widget_owner_ = nullptr;
};

wxDialog * CreateTestDialog(wxWindow *parent)
{
    return new TestFrame(parent);
}

NS_HWM_END
