#pragma once

NS_HWM_BEGIN

class IRenderableWindowBase
{
protected:
    IRenderableWindowBase()
    {}
    
public:
    virtual
    ~IRenderableWindowBase()
    {}
    
    void Render(wxDC &dc);
    
protected:
    bool is_shown_ = true;
    virtual
    void doRender(wxDC &dc) = 0;
};

template<class WindowType = wxWindow>
class IRenderableWindow
:   public WindowType
,   public IRenderableWindowBase
{
public:
    using WindowType::Bind;
    
    IRenderableWindow(wxWindow *parent, wxWindowID id = wxID_ANY,
                      wxPoint pos = wxDefaultPosition,
                      wxSize size = wxDefaultSize)
    :   WindowType(parent, id, pos, size)
    {
        Bind(wxEVT_PAINT, [this](auto &) {
            if(IsUsingDefaultPaintMethod()) {
                wxPaintDC dc(this);
                Render(dc);
            }
        });
    }
    
    void UseDefaultPaintMethod(bool flag) { use_default_paint_method_ = flag; }
    bool IsUsingDefaultPaintMethod() const { return use_default_paint_method_; }
    
    virtual bool Show(bool show = true) override
    {
        IRenderableWindowBase::is_shown_ = show;
        return WindowType::Show(show);
    }
    
private:
    bool use_default_paint_method_ = true;
};

class ImageButton
:   public IRenderableWindow<>
{
public:
    ImageButton(wxWindow *parent,
                bool is_3state,
                wxImage normal,
                wxImage hover,
                wxImage pushed,
                wxImage hover_pushed,
                wxPoint pos = wxDefaultPosition,
                wxSize size = wxDefaultSize);
    
    bool IsPushed() const;
    void SetPushed(bool status);
    
    bool Layout() override;
    
    //bool Enable(bool enable = true) override;
    void doRender(wxDC &dc) override;
    
private:
    wxImage normal_;
    wxImage hover_;
    wxImage pushed_;
    wxImage hover_pushed_;
    wxImage orig_normal_;
    wxImage orig_hover_;
    wxImage orig_pushed_;
    wxImage orig_hover_pushed_;
    bool is_hover_ = false;
    bool is_3state_ = false;
    bool is_being_pressed_ = false;
    bool is_pushed_ = false;
};

class ImageAsset
{
public:
    ImageAsset()
    :   num_cols_(0)
    ,   num_rows_(0)
    {}
    
    ImageAsset(String filepath, int num_cols, int num_rows)
    :   num_cols_(num_cols)
    ,   num_rows_(num_rows)
    {
        assert(num_cols_ >= 1);
        assert(num_rows_ >= 1);
        
        image_ = wxImage(filepath);
        
        assert(image_.GetWidth() % num_cols_ == 0);
        assert(image_.GetHeight() % num_rows_ == 0);
    }
    
    wxImage GetImage(int col, int row) const
    {
        assert(0 <= col && col < num_cols_);
        assert(0 <= row && row < num_rows_);
        
        auto const w = image_.GetWidth() / num_cols_;
        auto const h = image_.GetHeight() / num_rows_;
        
        wxRect r(wxPoint(w * col, h * row), wxSize(w, h));
        return image_.GetSubImage(r);
    }
    
private:
    wxImage image_;
    int num_cols_;
    int num_rows_;
};

class Label
:   public IRenderableWindow<>
{
public:
    Label(wxWindow *parent);
   
    bool AcceptsFocus() const override;
    
    void doRender(wxDC &dc) override;
    
    void SetText(wxString new_text);
    wxString GetText() const;
    
    void SetAlignment(int align);
    int GetAlignment() const;
    
private:
    wxString text_;
    int align_ = 0;
};

NS_HWM_END
