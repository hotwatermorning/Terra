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
    ScopedTranslateDC(wxDC &dc, FSize size)
    :   dc_(&dc)
    ,   specified_size_(size)
    {
        saved_size_ = dc_->GetLogicalOrigin();
        applied_size_ = wxPoint {
            (int)std::round(saved_size_.x + specified_size_.w),
            (int)std::round(saved_size_.y + specified_size_.h)
        };

        dc_->SetLogicalOrigin(applied_size_.x, applied_size_.y);
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
        dc_->SetLogicalOrigin(saved_size_.x, saved_size_.y);
        dc_ = nullptr;
    }

    wxPoint GetAppliedSize() const noexcept { return applied_size_; }

private:
    wxDC *dc_ = nullptr;
    FSize specified_size_;
    wxPoint applied_size_;
    wxPoint saved_size_;
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

    bool isIntersected(FPoint const line_begin, FPoint const line_end) const
    {
        int const kNumMaxCompletion = 3000;

        wxRegion r_ch;
        wxPoint pt_ch[4];
        pt_ch[0] = wxPoint { (int)pt_begin_.x, (int)pt_begin_.y };
        pt_ch[1] = wxPoint { (int)pt_control1_.x, (int)pt_control1_.y };
        pt_ch[2] = wxPoint { (int)pt_end_.x, (int)pt_end_.y };
        pt_ch[3] = wxPoint { (int)pt_control2_.x, (int)pt_control2_.y };

        for(int i = 0; i < 4; ++i) {
            wxPoint tmp_pts[3] { pt_ch[i], pt_ch[(i+1)%4], pt_ch[(i+2)%4] };
            wxRegion tmp(3, tmp_pts);
            r_ch.Union(tmp);
        }

        wxPoint pt_line[4];
        pt_line[0] = wxPoint { (int)line_begin.x, (int)line_begin.y };
        pt_line[1] = wxPoint { (int)line_end.x, (int)line_begin.y };
        pt_line[2] = wxPoint { (int)line_end.x, (int)line_end.y };
        pt_line[3] = wxPoint { (int)line_begin.x, (int)line_end.y };

        bool const dx = (line_begin.x < line_end.x) ? 1 : -1;
        bool const dy = (line_begin.y < line_end.y) ? 1 : -1;

        pt_line[0].x -= dx;
        pt_line[3].x -= dx;
        pt_line[1].x += dx;
        pt_line[2].x += dx;

        pt_line[0].y -= dy;
        pt_line[1].y -= dy;
        pt_line[2].y += dy;
        pt_line[3].y += dy;

        wxRegion line(4, pt_line);
        r_ch.Intersect(line);
        bool is_intersected_with_convex_hull = !r_ch.IsEmpty();

        // 指定した直線がベジエ曲線の凸包と交差するかどうか。

        if(is_intersected_with_convex_hull == false) { return false; }

        auto diff = line_end - line_begin;
        auto const num_compl = std::min<int>(std::max<int>(fabs(diff.w), fabs(diff.h)), kNumMaxCompletion);

        auto pt_last = get(0);
        for(int i = 1; i <= num_compl; ++i) {
            auto pt = get(i / (double)num_compl);

            if(isLinesIntersected(line_begin, line_end, pt_last, pt)) {
                return true;
            }

            pt_last = pt;
        }

        return false;
    }

    FPoint pt_begin_;
    FPoint pt_end_;

    FPoint pt_control1_;
    FPoint pt_control2_;
};

NS_HWM_END

