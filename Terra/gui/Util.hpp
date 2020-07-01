#pragma once

#include "./DataType.hpp"

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

    explicit
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

struct [[nodiscard]] ScopedTranslateDC
{
    ScopedTranslateDC(wxDC &dc, FSize size, bool device_origin = false)
    :   dc_(&dc)
    {
        if(device_origin) {
            dc_->GetDeviceOrigin(&saved_pos_x_, &saved_pos_y_);
            dc_->SetDeviceOrigin(saved_pos_x_ + size.w,
                                  saved_pos_y_ + size.h);
        } else {
            dc_->GetLogicalOrigin(&saved_pos_x_, &saved_pos_y_);
            dc_->SetLogicalOrigin(saved_pos_x_ + size.w,
                                  saved_pos_y_ + size.h);
        }
    }

    ScopedTranslateDC(ScopedTranslateDC const &rhs) = delete;
    ScopedTranslateDC & operator=(ScopedTranslateDC const &rhs) = delete;
    ScopedTranslateDC(ScopedTranslateDC &&rhs) = delete;
    ScopedTranslateDC & operator=(ScopedTranslateDC &&rhs) = delete;

    ~ScopedTranslateDC()
    {
        reset();
    }

    void reset()
    {
        if(!dc_) { return; }
        dc_->SetLogicalOrigin(saved_pos_x_, saved_pos_y_);
        dc_ = nullptr;
    }

private:
    wxDC *dc_ = nullptr;
    int saved_pos_x_ = 0;
    int saved_pos_y_ = 0;
};

struct [[nodiscard]] ScopedScaleDC
{
    ScopedScaleDC(wxDC &dc, double scale)
    :   ScopedScaleDC(dc, scale, scale)
    {}

    ScopedScaleDC(wxDC &dc, double scale_x, double scale_y)
    :   dc_(&dc)
    {
        dc_->GetUserScale(&saved_scale_x_, &saved_scale_y_);
        dc_->SetUserScale(saved_scale_x_ * scale_x, saved_scale_y_ * scale_y);
    }

    ScopedScaleDC(ScopedScaleDC const &rhs) = delete;
    ScopedScaleDC & operator=(ScopedScaleDC const &rhs) = delete;
    ScopedScaleDC(ScopedScaleDC &&rhs) = delete;
    ScopedScaleDC & operator=(ScopedScaleDC &&rhs) = delete;

    ~ScopedScaleDC()
    {
        reset();
    }

    void reset()
    {
        if(!dc_) { return; }
        dc_->SetUserScale(saved_scale_x_, saved_scale_y_);
        dc_ = nullptr;
    }

private:
    wxDC *dc_ = nullptr;
    double saved_scale_x_ = 1.0;
    double saved_scale_y_ = 1.0;
};

NS_HWM_END

