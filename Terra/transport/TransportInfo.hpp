#pragma once

NS_HWM_BEGIN

struct TransportInfo {
    double sample_rate_ = 44100.0;

    SampleCount smp_begin_pos_ = 0;
    SampleCount smp_end_pos_ = 0;
    double ppq_begin_pos_ = 0;
    double ppq_end_pos_ = 0;
    bool playing_ = 0;
    bool loop_enabled_ = false;
    SampleCount loop_begin_ = 0;
    SampleCount loop_end_ = 0;
    double tempo_ = 120.0;
    UInt8 time_sig_numer_ = 4;
    UInt8 time_sig_denom_ = 4;
    
    SampleCount GetSmpDuration() const {
        return smp_end_pos_ - smp_begin_pos_;
    }
    
    bool IsLooping() const {
        return loop_enabled_ && (loop_begin_ < loop_end_);
    }
};

NS_HWM_END
