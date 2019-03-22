#include "./Util.hpp"

NS_HWM_BEGIN

wxColour HSVToColour(float hue, float saturation, float value, float opaque)
{
    assert(0 <= hue && hue <= 1.0);
    assert(0 <= saturation && saturation <= 1.0);
    assert(0 <= value && value <= 1.0);
    assert(0 <= opaque && opaque <= 1.0);
    
    wxImage::HSVValue hsv { hue, saturation, value };
    auto rgb = wxImage::HSVtoRGB(hsv);
    wxColour col;
    col.Set(rgb.red, rgb.green, rgb.blue, std::min<int>(std::round(opaque * 256), 255));
    return col;
}

void ClearImage(wxImage &img)
{
    img.Clear();
    if (img.HasAlpha()) {
        std::fill_n(img.GetAlpha(), img.GetWidth() * img.GetHeight(), 0);
    }
}

NS_HWM_END
