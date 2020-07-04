#pragma once

#include "./Widget.hpp"

NS_HWM_BEGIN

//! Button class.
/*! this class send wxEVT_BUTTON message on pushed if `IsToggleModeEnabled() == false`,
 *  send wxEVT_TOGGLEBUTTON message otherwise.
 */
class Button
:   public IWidget
,   public wxEvtHandler
{
public:
    //! Button appearance setting.
    struct ButtonTheme
    {
        //! Colour settings.
        struct ColourSetting {
            //! colour saturation amount of the button. (0.0f .. 1.0f)
            float sa_front_;

            //! brightness of the background edge highlight. (0.0f .. 1.0f)
            float br_background_edge_;

            //! brightness of background. (0.0f .. 1.0f)
            float br_background_;

            //! brightness of the front edge highlight. (0.0f .. 1.0f)
            float br_front_edge_;

            //! the top side brightness of front gradient color. (0.0f .. 1.0f)
            float br_front_grad_top_;

            //! the bottom side brightness of the front gradient color. (0.0f .. 1.0f)
            float br_front_grad_bottom_;

            //! brightness of the text highlight. (0.0f .. 1.0f)
            float br_text_edge_;

            //! brightness of the text. (0.0f .. 1.0f)
            float br_text_;
        };

        //! rounding size of the button's rectangle area.
        float sz_round_;

        //! edge highlight size.
        float sz_edge_;

        //! Colour setting for normal state.
        ColourSetting normal_;

        //! Colour setting for highlighted state.
        ColourSetting highlighted_;

        //! Colour setting for down state.
        ColourSetting down_;
    };

    //! Get the default button theme.
    static
    ButtonTheme getDefaultButtonTheme() noexcept;

    //! constructor
    /*! @param name the initial text of the button.
     *  @param hue the base color of the button. (0.0f .. 1.0f)
     *  @post IsToggleModeEnabled() == false
     */
    Button(FPoint pos = FPoint {},
           FSize size = FSize {},
           ButtonTheme theme = getDefaultButtonTheme()
           );

    //! Set the base color of the button.
    /*! @param hue the base color of the button. (0.0f .. 1.0f)
     */
    void setHue(float hue) noexcept;

    //! Get the base color of the button.
    float getHue() const noexcept;

    //! Set button theme
    void setButtonTheme(ButtonTheme const &theme) noexcept;

    //! Get button theme
    ButtonTheme getButtonTheme() const noexcept;

    //! Enable or disable toggle mode.
    /*! If `to_enable == false && IsPushed()`,
     *  this class resets its pushed state and sends wxEVT_TOGGLEBUTTON message
     *  before disabling toggle mode.
     */
    void EnableToggleMode(bool to_enable) noexcept;

    //! Get toggle mode.
    bool IsToggleModeEnabled() const noexcept;

    //! Get the toggle button has been pushed down.
    /*! @note only meaningful for when `IsToggleModeEnabled() == true`
     */
    bool IsPushed() const noexcept;

    //! Get the button is currently being pushed.
    bool IsBeingPushed() const noexcept;

private:
    float hue_ = 0.0;
    ButtonTheme theme_;
    bool being_pushed_ = false;
    bool pushed_ = false;
    bool hover_ = false;
    bool toggle_mode_ = false;

    void OnLeftDown(MouseEvent &ev) override;
    void OnLeftUp(MouseEvent &ev) override;
    void OnMouseEnter(MouseEvent &ev) override;
    void OnMouseLeave(MouseEvent &ev) override;
    void OnMouseMove(MouseEvent &ev) override;
    void OnPaint(wxDC &dc) override;
    void OnMouseCaptureLost(MouseCaptureLostEvent &ev) override;

    virtual
    void paintButton(wxDC &dc,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown
                     );
};

NS_HWM_END
