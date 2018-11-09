#include "Transporter.hpp"

NS_HWM_BEGIN

SampleCount Transporter::GetCurrentPos() const
{
    auto lock = lf_.make_lock();
    return transport_info_.sample_pos_;
}

double GetPPQPos(TransportInfo const &info)
{
    double sec_pos = info.sample_pos_ / info.sample_rate_;
    double ppq_per_sec = info.tempo_ * 60.0;
    return sec_pos * ppq_per_sec;
}

void Transporter::MoveTo(SampleCount pos)
{
    auto lock = lf_.make_lock();
    transport_info_.sample_pos_ = pos;
    transport_info_.ppq_pos_ = GetPPQPos(transport_info_);
}

bool Transporter::IsPlaying() const {
    auto lock = lf_.make_lock();
    return transport_info_.playing_;
}

void Transporter::SetPlaying(bool is_playing)
{
    auto lock = lf_.make_lock();
    transport_info_.playing_ = is_playing;
}

void Transporter::SetLoopRange(SampleCount begin, SampleCount end)
{
    assert(0 <= begin);
    assert(begin <= end);
    auto lock = lf_.make_lock();
    transport_info_.loop_begin_ = begin;
    transport_info_.loop_end_ = end;
}

void Transporter::SetLoopEnabled(bool enabled)
{
    auto lock = lf_.make_lock();
    transport_info_.loop_enabled_ = enabled;
}

std::pair<SampleCount, SampleCount> Transporter::GetLoopRange() const
{
    auto lock = lf_.make_lock();
    return std::pair{ transport_info_.loop_begin_, transport_info_.loop_end_ };
}

bool Transporter::IsLoopEnabled() const
{
    auto lock = lf_.make_lock();
    return transport_info_.loop_enabled_;
}

void Transporter::Traverse(SampleCount length, ITraversalCallback *cb)
{
    SampleCount remain = length;
    
    for( ; remain > 0 ; ) {
        TransportInfo ti = (lf_.make_lock(),transport_info_);
        
        auto num_to_process = 0;
        bool jump_to_begin = 0;
        
        if(ti.IsLoopValid() && ti.playing_) {
            if(ti.sample_pos_ < ti.loop_begin_) {
                num_to_process = std::min(ti.sample_pos_ + remain, ti.loop_begin_) - ti.sample_pos_;
            } else if(ti.sample_pos_ < ti.loop_end_) {
                num_to_process = std::min(ti.sample_pos_ + remain, ti.loop_end_) - ti.sample_pos_;
                if(ti.sample_pos_ + num_to_process == ti.loop_end_) {
                    jump_to_begin = true;
                }
            } else {
                num_to_process = remain;
            }
        } else {
            num_to_process = remain;
        }
        
        cb->Process(ti, num_to_process);
        
        auto lock = lf_.make_lock();
        if(transport_info_.sample_pos_ == ti.sample_pos_ && ti.playing_) {
            if(jump_to_begin) {
                transport_info_.sample_pos_ = transport_info_.loop_begin_;
            } else {
                transport_info_.sample_pos_ += num_to_process;
            }
            transport_info_.ppq_pos_ = GetPPQPos(transport_info_);
            transport_info_.last_end_pos_ = ti.sample_pos_ + num_to_process;
        }
        lock.unlock();
        
        remain -= num_to_process;
    }
}

NS_HWM_END
