#include "Traverser.hpp"

NS_HWM_BEGIN

double GetPPQPos(TransportInfo const &info);

Transporter::Traverser::Traverser()
{}

void Transporter::Traverser::Traverse(Transporter *tp, SampleCount length, ITraversalCallback *cb)
{
    SampleCount remain = length;
    
    for( ; remain > 0 ; ) {
        TransportInfo ti;
        {
            auto lock = tp->lf_.make_lock();
            ti = tp->transport_info_;
        }
        
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
        
        auto lock = tp->lf_.make_lock();
        auto &current = tp->transport_info_;
        if(current.sample_pos_ == ti.sample_pos_ && ti.playing_) {
            if(jump_to_begin) {
                current.sample_pos_ = current.loop_begin_;
            } else {
                current.sample_pos_ += num_to_process;
            }
            current.ppq_pos_ = GetPPQPos(current);
            current.last_end_pos_ = ti.sample_pos_ + num_to_process;
        }
        lock.unlock();
        
        remain -= num_to_process;
    }
}

NS_HWM_END
