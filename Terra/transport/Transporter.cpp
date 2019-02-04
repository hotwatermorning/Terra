#include "Transporter.hpp"
#include "../misc/Round.hpp"

NS_HWM_BEGIN

template<class F>
void Transporter::AlterTransportInfo(F f)
{
    TransportInfo old_info;
    TransportInfo new_info;
    
    auto lock = lf_.make_lock();
    old_info = transport_info_;
    f(transport_info_);
    new_info = transport_info_;
    lock.unlock();
    
    listeners_.Invoke([&](auto *li) {
        li->OnChanged(old_info, new_info);
    });
}

Transporter::Transporter(IMusicalTimeService const *mt)
:   mt_(mt)
{
    assert(mt_ != nullptr);
}

Transporter::~Transporter()
{}

IMusicalTimeService const * Transporter::GetMusicalTimeService() const
{
    return mt_;
}

void Transporter::AddListener(ITransportStateListener *li)
{
    listeners_.AddListener(li);
}

void Transporter::RemoveListener(ITransportStateListener const *li)
{
    listeners_.RemoveListener(li);
}

TransportInfo Transporter::GetCurrentState() const
{
    auto lock = lf_.make_lock();
    return transport_info_;
}

bool Transporter::IsPlaying() const
{
    auto lock = lf_.make_lock();
    return transport_info_.playing_;
}

TimeRange Transporter::GetLoopRange() const
{
    auto lock = lf_.make_lock();
    return transport_info_.loop_;
}

bool Transporter::IsLoopEnabled() const
{
    auto lock = lf_.make_lock();
    return transport_info_.loop_enabled_;
}

void Transporter::MoveTo(SampleCount pos)
{
    auto time_point = SampleToTimePoint(pos);
    
    AlterTransportInfo([&time_point, this](TransportInfo &info) {
        info.play_ = TimeRange(time_point, time_point);
        last_moved_pos_ = time_point;
    });
}

void Transporter::Rewind(Tick tolerance)
{
    auto const current = GetCurrentState();
    auto const tick = Round<Tick>(current.play_.begin_.tick_);
    auto const mbt = mt_->TickToMBT(tick);
    auto const meter = mt_->GetMeterAt(tick);
    auto const tpqn = mt_->GetTpqn();
    
    auto const beat_len = meter.GetBeatLength(tpqn);
    auto const tick_from_measure = beat_len * mbt.beat_ + mbt.tick_;
    
    UInt32 new_measure = 0;
    if(tick_from_measure <= tolerance) {
        new_measure = std::max<Int32>(mbt.measure_, 1) - 1;
    } else {
        new_measure = mbt.measure_;
    }
    
    auto const new_tick = mt_->MBTToTick(MBT(new_measure, 0, 0));
    auto const new_sample = Round<SampleCount>(mt_->TickToSample(new_tick));
    auto const new_time_point = SampleToTimePoint(new_sample);
    
    AlterTransportInfo([&new_time_point](TransportInfo &info) {
        info.play_ = TimeRange(new_time_point, new_time_point);
    });
}

void Transporter::FastForward()
{
    auto const current = GetCurrentState();
    auto const tick = Round<Tick>(current.play_.begin_.tick_);
    auto const mbt = mt_->TickToMBT(tick);
    
    UInt32 const new_measure = mbt.measure_ + 1;
    auto const new_tick = mt_->MBTToTick(MBT(new_measure, 0, 0));
    auto const new_sample = Round<SampleCount>(mt_->TickToSample(new_tick));
    auto const new_time_point = SampleToTimePoint(new_sample);
    
    AlterTransportInfo([&new_time_point](TransportInfo &info) {
        info.play_ = TimeRange(new_time_point, new_time_point);
    });
}

void Transporter::SetStop()
{
    AlterTransportInfo([this](TransportInfo &info) {
        info.playing_ = false;
        info.play_ = TimeRange(last_moved_pos_, last_moved_pos_);
    });
}

void Transporter::SetPlaying(bool is_playing)
{
    AlterTransportInfo([is_playing](TransportInfo &info) {
        info.playing_ = is_playing;
    });
}

void Transporter::SetLoopRange(SampleCount begin, SampleCount end)
{
    assert(0 <= begin);
    assert(begin <= end);
    
    auto tp_begin = SampleToTimePoint(begin);
    auto tp_end = SampleToTimePoint(end);
    
    AlterTransportInfo([&tp_begin, &tp_end](TransportInfo &info) {
        info.loop_ = TimeRange(tp_begin, tp_end);
    });
}

void Transporter::SetLoopEnabled(bool enabled)
{
    AlterTransportInfo([enabled](TransportInfo &info) {
        info.loop_enabled_ = enabled;
    });
}

TimePoint Transporter::SampleToTimePoint(SampleCount sample) const
{
    TimePoint tp;
    tp.sample_ = sample;
    tp.tick_ = mt_->SampleToTick(sample);
    tp.sec_ = mt_->SampleToSec(sample);
    
    return tp;
}

NS_HWM_END
