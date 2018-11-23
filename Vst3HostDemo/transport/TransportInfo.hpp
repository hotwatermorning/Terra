#pragma once

NS_HWM_BEGIN

struct TransportInfo {
    double sample_rate_ = 44100.0;
    
    //! last changed position
    SampleCount last_moved_pos_ = 0;
    SampleCount sample_pos_ = 0;
    SampleCount last_end_pos_ = 0;
    double ppq_pos_ = 0;
    bool playing_ = 0;
    bool loop_enabled_ = false;
    SampleCount loop_begin_ = 0;
    SampleCount loop_end_ = 0;
    double tempo_ = 120.0;
    int time_sig_numer_ = 4;
    int time_sig_denom_ = 4;
    
    bool IsLoopValid() const {
        return loop_enabled_ && (loop_begin_ < loop_end_);
    }
};

NS_HWM_END
