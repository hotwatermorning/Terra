#pragma once

#include "./DataType.hpp"
#include "./Util.hpp"

NS_HWM_BEGIN

class IWidget;

class IWidgetOwner
{
protected:
    IWidgetOwner() {}

public:
    virtual ~IWidgetOwner() {}

    virtual
    void RequestToRedraw(FRect rc) = 0;

    virtual
    void CaptureMouse(IWidget *p) = 0;

    virtual
    void ReleaseMouse(IWidget *p) = 0;
};

class IWidget
{
public:
    IWidget()
    :   IWidget(FPoint{0, 0}, FSize{0, 0})
    {}

    IWidget(FPoint pos, FSize size)
    {
        rc_.pos = pos;
        rc_.size = size;
    }

    virtual
    void OnPaint(wxDC &dc)
    {}

    virtual
    void OnPaintChildren(wxDC &dc)
    {
        for(auto c: children_) {
            c->ProcessPaintEventFromParent(dc);
        }
    }

    void OnPaintOverChildren(wxDC &dc) {}

    void SetEnabled(bool flag)
    {
        enabled_ = flag;
        OnSetEnabled();
    }

    bool IsEnabled() const noexcept
    {
        return enabled_;
    }

    void SetVisible(bool flag)
    {
        visible_ = true;
        OnSetVisible();
    }

    bool IsVisible() const noexcept
    {
        return visible_;
    }

    FRect GetClientRect() const noexcept { return rc_.WithPosition(0, 0); }
    FRect GetRect() const noexcept { return rc_; }
    FPoint GetPosition() const noexcept { return rc_.pos; }
    FSize GetSize() const noexcept { return rc_.size; }

    void SetPosition(FPoint pos) {
        rc_.pos = pos;
        OnSetPosition();
    }

    void SetSize(FSize size) {
        rc_.size = size;
        OnSetSize();
    }

    String GetLabel() const { return label_; }
    void SetLabel(String const &label) { label_ = label; }

    void SetParent(IWidget *parent)
    {
        assert(owner_ == nullptr);
        parent_ = parent;
        OnSetParent();
    }

    void SetOwner(IWidgetOwner *owner)
    {
        assert(parent_ == nullptr);
        owner_ = owner;
        OnSetOwner();
    }

    struct MouseEvent
    {
        MouseEvent()
        {}

        MouseEvent(FPoint pt, wxKeyboardState key_state)
        :   pt_(pt)
        ,   key_state_(key_state)
        {}

        MouseEvent(FPoint pt, wxKeyboardState key_state, int wheel_delta, int wheel_rotation)
        :   pt_(pt)
        ,   key_state_(key_state)
        ,   wheel_delta_(wheel_delta)
        ,   wheel_rotation_(wheel_rotation)
        {}

        FPoint pt_;
        int wheel_delta_ = 0;
        int wheel_rotation_ = 0;
        wxKeyboardState key_state_;
        bool should_propagate_ = false;
    };

    struct KeyEvent
    {
        Int32 ascii_code_;
        wchar_t unicode_;
        UInt32 raw_key_code_;
        UInt32 raw_key_flags_;

        bool HasUnicode() const { return unicode_ != WXK_NONE; }

        FPoint pt_;
        wxKeyboardState key_state_;
        bool should_propagate_ = false;
    };

    struct MouseCaptureLostEvent
    {};

public:
    void ProcessLeftDown(MouseEvent &ev)
    {
        OnLeftDown(ev);
    }

    void ProcessLeftUp(MouseEvent &ev)
    {
        OnLeftUp(ev);
    }

    void ProcessLeftDoubleClick(MouseEvent &ev)
    {
        OnLeftDoubleClick(ev);
    }

    void ProcessRightDown(MouseEvent &ev)
    {
        OnRightDown(ev);
    }

    void ProcessRightUp(MouseEvent &ev)
    {
        OnRightUp(ev);
    }

    void ProcessRightDoubleClick(MouseEvent &ev)
    {
        OnRightDoubleClick(ev);
    }

    void ProcessMouseMove(MouseEvent &ev)
    {
        for(auto c: children_) {
            if(c->mouse_entered_) {
                MouseEvent tmp_ev;
                tmp_ev.pt_ = ev.pt_;
                tmp_ev.key_state_ = ev.key_state_;
                c->ProcessEventFromParent(tmp_ev, &IWidget::ProcessMouseLeave);
            }
        }

        if(mouse_entered_ == false) {
            auto tmp_ev = ev;
            ProcessMouseEnter(tmp_ev);
        }

        OnMouseMove(ev);
    }

    void ProcessMouseEnter(MouseEvent &ev)
    {
        mouse_entered_ = true;
        OnMouseEnter(ev);
    }

    void ProcessMouseLeave(MouseEvent &ev)
    {
        for(auto c: children_) {
            if(c->mouse_entered_) {
                MouseEvent tmp_ev;
                tmp_ev.pt_ = ev.pt_;
                tmp_ev.key_state_ = ev.key_state_;
                c->ProcessEventFromParent(tmp_ev, &IWidget::ProcessMouseLeave);
            }
        }

        if(mouse_entered_) {
            OnMouseLeave(ev);
            mouse_entered_ = false;
        }
    }

    void ProcessMouseWheel(MouseEvent &ev)
    {
        OnMouseWheel(ev);
    }

    void ProcessKeyDown(KeyEvent &ev)
    {
        OnKeyDown(ev);
    }

    void ProcessKeyUp(KeyEvent &ev)
    {
        OnKeyUp(ev);
    }

    void ProcessChar(KeyEvent &ev)
    {
        OnChar(ev);
    }

    void ProcessMouseCaptureLost(MouseCaptureLostEvent &ev)
    {
        OnMouseCaptureLost(ev);
    }

    //=================================================================================================

    template<class EventType>
    void ProcessEventFromParent(EventType &ev, void(IWidget::* handler)(EventType &ev), IWidget *target = nullptr)
    {
        auto pt = ParentToChild(ev.pt_);
        auto child = GetChildWithPosition(pt);
        if(child) {
            auto tmp_ev = ev;
            tmp_ev.pt_ = pt;
            child->ProcessEventFromParent(tmp_ev, handler, target);

            ev.should_propagate_ = tmp_ev.should_propagate_;
            if(ev.should_propagate_ == false) {
                return;
            }

            ev.should_propagate_ = true;
        }

        if(!target || target == this) {
            auto tmp_ev = ev;
            tmp_ev.pt_ = pt;
            (this->*handler)(tmp_ev);

            ev.should_propagate_ = tmp_ev.should_propagate_;
        }
    }

    void ProcessPaintEventFromParent(wxDC &dc)
    {
        if(redraw_area_.IsEmpty()) { return; }

        // ScopedClipDC sc(dc, wxRect(redraw_area_));

        ScopedTranslateDC st(dc, - GetPosition().AsSize());

        //auto as = st.GetAppliedSize();
        //ScopedClipDC sc(dc, (wxRect)redraw_area_.Translated(as.x, as.y));

        ScopedClipDC sc(dc, wxRect(redraw_area_));
        ScopedClipDC sc2(dc, wxRect(GetClientRect()));

        OnPaint(dc);

        for(auto c: children_) {
            auto rc_child = c->GetRect();
            if(redraw_area_.IsIntersected(rc_child)) {
                c->redraw_area_.Join(this->ParentToChild(redraw_area_));
            }
        }
        OnPaintChildren(dc);

        redraw_area_ = FRect{};
    }

    FPoint ChildToParent(FPoint pt) const
    {
        return pt + GetPosition().AsSize();
    }

    FPoint ParentToChild(FPoint pt) const
    {
        return pt - GetPosition().AsSize();
    }

    FRect ChildToParent(FRect rc) const
    {
        return FRect {
            ChildToParent(rc.GetTopLeft()),
            ChildToParent(rc.GetBottomRight())
        };
    }

    FRect ParentToChild(FRect rc) const
    {
        return FRect {
            ParentToChild(rc.GetTopLeft()),
            ParentToChild(rc.GetBottomRight())
        };
    }

    IWidget * GetParent() const noexcept { return parent_; }
    IWidgetOwner * GetOwner() const noexcept { return owner_; }

    void AddChild(IWidget *p) {
        assert(std::find(children_.begin(), children_.end(), p) == children_.end());
        children_.push_back(p);
        p->SetParent(this);
    }

    void RemoveChild(IWidget *p) {
        auto found = std::find(children_.begin(), children_.end(), p);
        if(found != children_.end()) {
            (*found)->SetParent(nullptr);
            children_.erase(found);
        }
    }

    //! Find child widgets by a position.
    IWidget * GetChildWithPosition(FPoint pt)
    {
        for(auto *c: children_) {
            if(c->GetClientRect().Contain(c->ParentToChild(pt))) {
                return c;
            }
        }

        return nullptr;
    }

    void Refresh()
    {
        Refresh(GetClientRect());
    }

    void Refresh(FRect rc)
    {
        RequestToRedraw(rc);
    }

    void CaptureMouse()
    {
        RequestToCaptureMouse(this);
    }

    void ReleaseMouse()
    {
        RequestToCaptureMouse(this);
    }

private:
    IWidget *parent_ = nullptr;
    IWidgetOwner *owner_ = nullptr;
    std::vector<IWidget *> children_; // does not own children.
    FRect rc_; // 親 Widget 上での位置

    bool being_pushed_ = false;
    bool pushed_ = false;
    bool mouse_entered_ = false;
    bool visible_ = false;
    bool enabled_ = true;
    FRect redraw_area_;
    String label_;

private:
    virtual void OnLeftDown(MouseEvent &ev) {}
    virtual void OnLeftUp(MouseEvent &ev) {}
    virtual void OnLeftDoubleClick(MouseEvent &ev) {}
    virtual void OnRightDown(MouseEvent &ev) {}
    virtual void OnRightUp(MouseEvent &ev) {}
    virtual void OnRightDoubleClick(MouseEvent &ev) {}
    virtual void OnMouseMove(MouseEvent &ev) {}
    virtual void OnMouseEnter(MouseEvent &ev) {}
    virtual void OnMouseLeave(MouseEvent &ev) {}
    virtual void OnMouseWheel(MouseEvent &ev) {}
    virtual void OnKeyDown(KeyEvent &ev) {}
    virtual void OnKeyUp(KeyEvent &ev) {}
    virtual void OnChar(KeyEvent &ev) {}
    virtual void OnMouseCaptureLost(MouseCaptureLostEvent &ev) {}
    virtual void OnSetVisible() {}
    virtual void OnSetEnabled() {}
    virtual void OnSetSize() {}
    virtual void OnSetPosition() {}
    virtual void OnSetParent() {}
    virtual void OnSetOwner() {}

    void RequestToRedraw(FRect rc)
    {
        redraw_area_.Join(rc.Intersected(GetClientRect()));
        
        if(auto p = GetParent()) {
            auto rc_in_parent = ChildToParent(redraw_area_);
            p->RequestToRedraw(rc_in_parent);
            return;
        }

        if(auto p = GetOwner()) {
            p->RequestToRedraw(redraw_area_);
            return;
        }
    }

    void RequestToCaptureMouse(IWidget *w)
    {
        if(auto p = GetParent()) {
            p->RequestToCaptureMouse(w);
            return;
        }

        if(auto p = GetOwner()) {
            p->CaptureMouse(w);
            return;
        }
    }

    void RequestToReleaseMouse(IWidget *w)
    {
        if(auto p = GetParent()) {
            p->RequestToReleaseMouse(w);
            return;
        }

        if(auto p = GetOwner()) {
            p->ReleaseMouse(w);
            return;
        }
    }
};

class WidgetOwnerWindow
:   public wxWindow
,   public IWidgetOwner
{
public:
    WidgetOwnerWindow(wxWindow *parent,
                      std::unique_ptr<IWidget> widget,
                      wxPoint pos = wxDefaultPosition,
                      wxSize size = wxDefaultSize)
    :   wxWindow(parent, wxID_ANY, pos, size)
    ,   widget_(std::move(widget))
    {
        widget_->SetOwner(this);

        Bind(wxEVT_PAINT, [this](wxPaintEvent &ev) { OnPaint(); });
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &ev) { OnKeyDown(ev); });
        Bind(wxEVT_KEY_UP, [this](wxKeyEvent &ev) { OnKeyUp(ev); });
        Bind(wxEVT_CHAR, [this](wxKeyEvent &ev) { OnChar(ev); });
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &ev) { OnLeftDown(ev); });
        Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &ev) { OnLeftUp(ev); });
        Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &ev) { OnLeftUp(ev); });
        Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent &ev) { OnRightDown(ev); });
        Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent &ev) { OnRightUp(ev); });
        Bind(wxEVT_RIGHT_DCLICK, [this](wxMouseEvent &ev) { OnRightUp(ev); });
        Bind(wxEVT_MOTION, [this](wxMouseEvent &ev) { OnMouseMove(ev); });
        Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &ev) { OnMouseWheel(ev); });
        Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &ev) { OnMouseEnter(ev); });
        Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &ev) { OnMouseLeave(ev); });
        Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &ev) { OnLeftDoubleClick(ev); });
        Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &ev) { OnCaptureLost(ev); });

        Bind(wxEVT_MOVE, [this](wxMoveEvent &ev) { OnMove(ev); });
        Bind(wxEVT_SIZE, [this](wxSizeEvent &ev) { OnSize(ev); });
    }

private:
    void OnPaint()
    {
        wxPaintDC dc(this);
        widget_->ProcessPaintEventFromParent(dc);
    }

    void OnKeyDown(wxKeyEvent &ev)
    {
        IWidget::KeyEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        tmp_ev.ascii_code_ = ev.GetKeyCode();
        tmp_ev.unicode_ = ev.GetUnicodeKey();
        tmp_ev.raw_key_code_ = ev.GetRawKeyCode();
        tmp_ev.raw_key_flags_ = ev.GetRawKeyFlags();
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessKeyDown);
    }

    void OnKeyUp(wxKeyEvent &ev)
    {
        IWidget::KeyEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        tmp_ev.ascii_code_ = ev.GetKeyCode();
        tmp_ev.unicode_ = ev.GetUnicodeKey();
        tmp_ev.raw_key_code_ = ev.GetRawKeyCode();
        tmp_ev.raw_key_flags_ = ev.GetRawKeyFlags();
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessKeyUp);
    }

    void OnChar(wxKeyEvent &ev)
    {
        IWidget::KeyEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        tmp_ev.ascii_code_ = ev.GetKeyCode();
        tmp_ev.unicode_ = ev.GetUnicodeKey();
        tmp_ev.raw_key_code_ = ev.GetRawKeyCode();
        tmp_ev.raw_key_flags_ = ev.GetRawKeyFlags();
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessChar);
    }

    void OnLeftDown(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessLeftDown, captured_);
    }

    void OnLeftUp(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessLeftUp, captured_);
    }

    void OnLeftDoubleClick(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessLeftDoubleClick, captured_);
    }

    void OnRightDown(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessRightDown, captured_);
    }

    void OnRightUp(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessRightUp, captured_);
    }

    void OnRightDoubleClick(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessRightDoubleClick, captured_);
    }

    void OnMouseMove(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessMouseMove, captured_);
    }

    void OnMouseEnter(wxMouseEvent &ev)
    {
//        IWidget::MouseEvent tmp_ev;
//        tmp_ev.pt_ = FPoint(ev.GetPosition());
//        tmp_ev.key_state_ = ev;
//        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessMouseEnter);
    }

    void OnMouseLeave(wxMouseEvent &ev)
    {
//        IWidget::MouseEvent tmp_ev;
//        tmp_ev.pt_ = FPoint(ev.GetPosition());
//        tmp_ev.key_state_ = ev;
//        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessMouseLeave);
    }

    void OnMouseWheel(wxMouseEvent &ev)
    {
        IWidget::MouseEvent tmp_ev;
        tmp_ev.pt_ = FPoint(ev.GetPosition());
        tmp_ev.key_state_ = ev;
        tmp_ev.wheel_delta_ = ev.GetWheelDelta();
        tmp_ev.wheel_rotation_ = ev.GetWheelRotation();
        widget_->ProcessEventFromParent(tmp_ev, &IWidget::ProcessMouseWheel, captured_);
    }

    void OnCaptureLost(wxMouseCaptureLostEvent &ev)
    {
        if(captured_) {
            IWidget::MouseCaptureLostEvent ev;
            captured_->ProcessMouseCaptureLost(ev);

            captured_ = nullptr;
        }
    }

    void OnMove(wxMoveEvent &ev)
    {
        Refresh();
    }

    void OnSize(wxSizeEvent &ev)
    {
        widget_->SetSize(FSize(ev.GetSize()));
    }

    void RequestToRedraw(FRect rc) override
    {
        wxRect wrc = rc;
        Refresh(true, &wrc);
    }

    void CaptureMouse(IWidget *p) override
    {
        if(captured_) {
            IWidget::MouseCaptureLostEvent ev;
            captured_->ProcessMouseCaptureLost(ev);
            captured_ = nullptr;
        }

        wxWindow::CaptureMouse();
        captured_ = p;
    }

    void ReleaseMouse(IWidget *p) override
    {
        if(captured_ == p) {
            wxWindow::ReleaseMouse();
            captured_ = nullptr;
        }
    }

private:
    std::unique_ptr<IWidget> widget_;
    IWidget *captured_ = nullptr;
};

NS_HWM_END
