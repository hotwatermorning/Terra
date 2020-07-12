#pragma once

#include "./Widget.hpp"

NS_HWM_BEGIN

//! This control sends wxEVT_SLIDER when the slider value is changed.
class Slider
:   public IWidget
{
public:
    explicit
    Slider(FPoint pos = { 0, 0 },
           FSize size = { 0, 0 });

    Slider(float value_min, float value_max, float value_default,
           FPoint pos = { 0, 0 },
           FSize size = { 0, 0 });

    ~Slider();

    float GetHue() const noexcept;
    void SetHue(float hue) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;

    void OnLeftDown(MouseEvent &ev) override;
    void OnLeftUp(MouseEvent &ev) override;
    void OnMouseMove(MouseEvent &ev) override;
    void OnPaint(wxDC &dc) override;
    void OnMouseEnter(MouseEvent &ev) override;
    void OnMouseLeave(MouseEvent &ev) override;
};

NS_HWM_END
