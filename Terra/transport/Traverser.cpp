#include "Traverser.hpp"

NS_HWM_BEGIN

double GetPPQPos(TransportInfo const &info);
double GetTempoAt(double ppq_pos);
std::pair<UInt8, UInt8> GetTimeSignatureAt(double ppq_pos);

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
        TransportInfo const orig = ti;
        
        bool need_jump_to_begin = false;
        
        if(ti.IsLooping() && ti.playing_) {
            if(ti.smp_begin_pos_ < ti.loop_begin_) {
                ti.smp_end_pos_ = std::min(ti.smp_begin_pos_ + remain, ti.loop_begin_);
            } else if(ti.smp_begin_pos_ < ti.loop_end_) {
                ti.smp_end_pos_ = std::min(ti.smp_begin_pos_ + remain, ti.loop_end_);
                if(ti.smp_end_pos_ == ti.loop_end_) {
                    need_jump_to_begin = true;
                }
            } else {
                ti.smp_end_pos_ = ti.smp_begin_pos_ + remain;
            }
        } else {
            ti.smp_end_pos_ = ti.smp_begin_pos_ + remain;
        }
        
        ti.ppq_end_pos_ = GetPPQPos(ti);
        ti.tempo_ = GetTempoAt(ti.ppq_begin_pos_);
        auto time_sig = GetTimeSignatureAt(ti.ppq_begin_pos_);
        ti.time_sig_numer_ = time_sig.first;
        ti.time_sig_denom_ = time_sig.second;
        
        cb->Process(ti);
        
        auto lock = tp->lf_.make_lock();
        auto &current = tp->transport_info_;
        if(current.smp_begin_pos_ == orig.smp_begin_pos_ &&
           current.ppq_begin_pos_ == orig.ppq_begin_pos_)
        {
            if(need_jump_to_begin) {
                current.smp_begin_pos_ = current.smp_end_pos_ = ti.loop_begin_;
                current.ppq_begin_pos_ = current.ppq_begin_pos_ = GetPPQPos(current);
            } else if(current.playing_) {
                current.smp_begin_pos_ = current.smp_end_pos_ = ti.smp_end_pos_;
                current.ppq_begin_pos_ = current.ppq_end_pos_ = ti.ppq_end_pos_;
            } else {
                current.smp_begin_pos_ = ti.smp_begin_pos_;
                current.smp_end_pos_ = ti.smp_end_pos_;
                current.ppq_begin_pos_ = ti.ppq_begin_pos_;
                current.ppq_end_pos_ = ti.ppq_end_pos_;
            }
        } // 再生位置が変わったときは、currentの状態をそのまま次回の再生に使用する
        lock.unlock();
        
        remain -= (ti.smp_end_pos_ - ti.smp_begin_pos_);
    }
}

NS_HWM_END
