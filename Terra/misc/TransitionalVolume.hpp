#pragma once

#include <atomic>

NS_HWM_BEGIN

//! 目的の音量に達するまでなめらかに音量を推移するクラス
class TransitionalVolume
{
public:
    //! コンストラクタ。指定されたパラメータから音量の推移する速度を計算して設定する。
    /*! @param sample_rate サンプリングレート
     *  @param duration_in_msec 音量が2倍や半分(= log10(2) * 2 =~ 6.02dB)に変化するのにかかる時間
     *  @param min_db 最小のdB値
     *  @param max_db 最大のdB値
     */
    TransitionalVolume(double sample_rate,
                       UInt32 duration_in_msec,
                       double min_db,
                       double max_db);
    
    TransitionalVolume();
    
    TransitionalVolume(TransitionalVolume const &) = delete;
    TransitionalVolume & operator=(TransitionalVolume const &) = delete;
    
    TransitionalVolume(TransitionalVolume &&rhs);
    TransitionalVolume & operator=(TransitionalVolume &&rhs);
    
    //! 指定したstepが経過したあとの音量値を計算してcurrent_db_に設定する
    //! この関数は、get_current_XXX()関数と同じスレッドから呼び出すこと
    void update_transition(Int32 step);
    
    //! 現在推移中の出力レベルをdB値として返す
    /*! @note この関数は、update_transition()関数と同じスレッドから呼び出すこと
     */
    double get_current_db() const;
    
    //! 現在推移中の出力レベルを線形なゲイン値として返す
    /*! `get_current_transition_db() == get_min_db()`のときは0を返す
     *  @note この関数は、update_transition()関数と同じスレッドから呼び出すこと
     */
    double get_current_linear_gain() const;
    
    //! コンストラクタで指定された、最小のdB値を返す。
    double get_min_db() const;
    //! コンストラクタで指定された、最大のdB値を返す。
    double get_max_db() const;

    //! `set_target_db()`関数で設定された出力レベルをdB値で返す。
    double get_target_db() const;
    
    //! 出力レベルを設定する
    /*! このクラスの現在の出力レベルは、コンストラクタで指定された速度で、このdB値に向かって推移する。
     */
    void set_target_db(double db);
    
private:
    double amount_;
    double min_db_;
    double max_db_;
    double current_db_ = 0;
    std::atomic<double> target_db_ = {0};
};

NS_HWM_END
