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

NS_HWM_END

