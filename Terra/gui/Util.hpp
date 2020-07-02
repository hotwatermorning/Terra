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

inline
bool isLinesIntersected(FPoint line1_begin, FPoint line1_end,
                        FPoint line2_begin, FPoint line2_end)
{
    auto const &a = line1_begin;
    auto const &b = line1_end;
    auto const &c = line2_begin;
    auto const &d = line2_end;

    auto const ta = (c.x - d.x) * (a.y - c.y) + (c.y - d.y) * (c.x - a.x);
    auto const tb = (c.x - d.x) * (b.y - c.y) + (c.y - d.y) * (c.x - b.x);
    auto const tc = (a.x - b.x) * (c.y - a.y) + (a.y - b.y) * (a.x - c.x);
    auto const td = (a.x - b.x) * (d.y - a.y) + (a.y - b.y) * (a.x - d.x);

    return tc * td < 0 && ta * tb < 0;
}

//! cubic bezier curve class.
struct Bezier
{
    Bezier()
    {}

    FPoint get(float t) const
    {
        assert(0.0 <= t && t <= 1.0);
        auto const &p1 = pt_begin_;
        auto const &p2 = pt_control1_;
        auto const &p3 = pt_control2_;
        auto const &p4 = pt_end_;

        auto cube = [](auto x) { return x * x * x; };
        auto square = [](auto x) { return x * x; };
        return FPoint {
            cube(1 - t) * p1.x + 3 * square(1 - t) * t * p2.x + 3 * (1 - t) * square(t) * p3.x + cube(t) * p4.x,
            cube(1 - t) * p1.y + 3 * square(1 - t) * t * p2.y + 3 * (1 - t) * square(t) * p3.y + cube(t) * p4.y,
        };
    }

    bool isIntersected(FPoint line_begin, FPoint line_end) const
    {
        int const kNumMaxCompletion = 3000;

        // pt1 と pt2 で表す直線がベジエ曲線の凸包と交差するかどうか。
        bool is_intersected_convex_hull = true;

        if(is_intersected_convex_hull == false) { return false; }

        auto diff = line_end - line_begin;
        auto const num_compl = std::min<int>(std::max<int>(fabs(diff.w), fabs(diff.h)), kNumMaxCompletion);

        auto pt_last = get(0);
        for(int i = 1; i <= num_compl; ++i) {
            auto pt = get(i / (double)num_compl);

            if(isLinesIntersected(line_begin, line_end, pt_last, pt)) {
                return true;
            }
        }

        return false;
    }

    FPoint pt_begin_;
    FPoint pt_end_;

    FPoint pt_control1_;
    FPoint pt_control2_;
};

NS_HWM_END

