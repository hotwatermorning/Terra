#pragma once

NS_HWM_BEGIN

wxColour HSVToColour(float hue, float saturation, float value, float opaque = 1.0);

class BrushPen
{
public:
    BrushPen(wxColour col) : BrushPen(col, col) {}
    
    BrushPen(wxColour brush, wxColour pen)
    : brush_(wxBrush(brush))
    , pen_(wxPen(pen))
    {}
    
    BrushPen(wxColour brush, wxColour pen, int pen_width)
    : brush_(wxBrush(brush))
    , pen_(wxPen(pen, pen_width))
    {}
    
    wxBrush brush_;
    wxPen pen_;
    
    void ApplyTo(wxDC &dc) const {
        dc.SetBrush(brush_);
        dc.SetPen(pen_);
    }
};

struct BrushPenSet
{
    BrushPen normal_;
    BrushPen hover_;
    BrushPen selected_;
};

void ClearImage(wxImage &img);

class GraphicsBuffer
{
public:
    GraphicsBuffer()
    {}

    GraphicsBuffer(wxSize size)
    {
        wxImage image(size);
        image.InitAlpha();
        bitmap_ = wxBitmap(image, 32);
        Clear();
    }

    GraphicsBuffer(GraphicsBuffer &&rhs)
    {
        bitmap_ = rhs.bitmap_;
        rhs.bitmap_ = wxBitmap();
    }

    GraphicsBuffer & operator=(GraphicsBuffer &&rhs)
    {
        bitmap_ = rhs.bitmap_;
        rhs.bitmap_ = wxBitmap();
        return *this;
    }

    GraphicsBuffer(GraphicsBuffer const &) = delete;
    GraphicsBuffer & operator=(GraphicsBuffer const &) = delete;

    bool IsOk() const { return bitmap_.IsOk(); }

    void Clear()
    {
        assert(IsOk());
        
        wxMemoryDC memory_dc(bitmap_);
        wxGCDC dc(memory_dc);
        dc.SetBackground(wxBrush(wxTransparentColour));
        dc.Clear();
    }

    wxBitmap & GetBitmap()
    {
        assert(IsOk());
        return bitmap_;
    }

    wxBitmap const & GetBitmap() const
    {
        assert(IsOk());
        return bitmap_;
    }

private:
    wxBitmap bitmap_;
};

inline
void transpose(wxPoint &pt) { std::swap(pt.x, pt.y); }

inline
void transpose(wxSize &size) { std::swap(size.x, size.y); }

inline
void transpose(wxRect &rc) { std::swap(rc.x, rc.y); std::swap(rc.width, rc.height); }

template<class T>
[[nodiscard]]
T transposed(T const &v) { auto tmp = v; transpose(tmp); return tmp; }

NS_HWM_END

