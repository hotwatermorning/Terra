#include "Traverser.hpp"

NS_HWM_BEGIN

Transporter::Traverser::Traverser()
{}

void Transporter::Traverser::Traverse(Transporter *tp, SampleCount length, ITraversalCallback *cb)
{
    SampleCount remain = length;
    
    for( ; remain > 0 ; ) {
        // 現在のTransportInfoのend位置を更新してフレーム処理。
        // そのあと、begin位置を更新して、次のフレームへ。
        TransportInfo ti;
        {
            auto lock = tp->lf_.make_lock();
            ti = tp->transport_info_;
        }
        TransportInfo const orig = ti;
        
        bool need_jump_to_begin = false;
        
        if(ti.IsLooping() && ti.playing_) {
            if(ti.play_.begin_.sample_ < ti.loop_.begin_.sample_) {
                ti.play_.end_.sample_ = std::min(ti.play_.begin_.sample_ + remain, ti.loop_.begin_.sample_);
            } else if(ti.play_.begin_.sample_ < ti.loop_.end_.sample_) {
                ti.play_.end_.sample_ = std::min(ti.play_.begin_.sample_ + remain, ti.loop_.end_.sample_);
                if(ti.play_.end_.sample_ == ti.loop_.end_.sample_) {
                    need_jump_to_begin = true;
                }
            } else {
                ti.play_.end_.sample_ = ti.play_.begin_.sample_ + remain;
            }
        } else {
            ti.play_.end_.sample_ = ti.play_.begin_.sample_ + remain;
        }
        
        auto mt = tp->GetMusicalTimeService();
        
        ti.sample_rate_ = mt->GetSampleRate();
        ti.tpqn_ = mt->GetTpqn();
        ti.play_ = TimeRange(ti.play_.begin_, tp->SampleToTimePoint(ti.play_.end_.sample_));
        ti.tempo_ = mt->GetTempoAt(ti.play_.begin_.tick_);
        ti.meter_ = mt->GetMeterAt(ti.play_.begin_.tick_);
        
        cb->Process(ti);
        
        remain -= ti.play_.duration_.sample_;
        
        auto lock = tp->lf_.make_lock();
        auto &current = tp->transport_info_;
        if(current.play_.begin_ == orig.play_.begin_) {
            if(need_jump_to_begin) {
                current.play_ = TimeRange(ti.loop_.begin_, ti.loop_.begin_);
            } else if(current.playing_) {
                current.play_ = TimeRange(ti.play_.end_, ti.play_.end_);
            } else {
                current.play_ = ti.play_;
            }
        } // 再生位置が変わったときは、currentの状態をそのまま次回の再生に使用する
        lock.unlock();
    }
}

NS_HWM_END
