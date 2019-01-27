#include "Transporter.hpp"

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

double GetPPQPos(TransportInfo const &info)
{
    double sec_pos = info.smp_begin_pos_ / info.sample_rate_;
    double ppq_per_sec = info.tempo_ / 60.0;
    return sec_pos * ppq_per_sec;
}

SampleCount GetSamplePos(TransportInfo const &info)
{
    assert(info.tempo_ > 0);
    double sec_per_ppq = 60.0 / info.tempo_;
    double sec_pos = info.ppq_begin_pos_ * sec_per_ppq;
    return (SampleCount)std::round(sec_pos * info.sample_rate_);
}

double GetTempoAt(double ppq_pos)
{
    return 120.0;
}

std::pair<UInt8, UInt8> GetTimeSignatureAt(double ppq_pos)
{
    return { 4, 4 };
}

Transporter::Transporter()
{}

Transporter::~Transporter()
{}

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

void Transporter::MoveTo(SampleCount pos)
{
    AlterTransportInfo([pos, this](TransportInfo &info) {
        info.smp_begin_pos_ = info.smp_end_pos_ = pos;
        info.ppq_begin_pos_ = info.ppq_end_pos_ = GetPPQPos(info);
        smp_last_moved_pos_ = pos;
    });
}

void Transporter::Rewind()
{
    AlterTransportInfo([](TransportInfo &info) {
        auto const measure = 4.0 * info.time_sig_numer_ / info.time_sig_denom_;
        info.ppq_begin_pos_ = info.ppq_end_pos_ = std::max<double>(0, info.ppq_begin_pos_ - measure);
        info.smp_begin_pos_ = info.smp_end_pos_ = GetSamplePos(info);
    });
}

void Transporter::FastForward()
{
    AlterTransportInfo([](TransportInfo &info) {
        auto const measure = 4.0 * info.time_sig_numer_ / info.time_sig_denom_;
        info.ppq_begin_pos_ = info.ppq_end_pos_ = info.ppq_begin_pos_ + measure;
        info.smp_begin_pos_ = info.smp_end_pos_ = GetSamplePos(info);
    });
}

bool Transporter::IsPlaying() const {
    auto lock = lf_.make_lock();
    return transport_info_.playing_;
}

void Transporter::SetStop()
{
    AlterTransportInfo([this](TransportInfo &info) {
        info.playing_ = false;
        info.smp_begin_pos_ = info.smp_end_pos_ = smp_last_moved_pos_;
        info.ppq_begin_pos_ = info.ppq_end_pos_ = GetPPQPos(info);
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
    
    AlterTransportInfo([begin, end](TransportInfo &info) {
        info.loop_begin_ = begin;
        info.loop_end_ = end;
    });
}

void Transporter::SetLoopEnabled(bool enabled)
{
    AlterTransportInfo([enabled](TransportInfo &info) {
        info.loop_enabled_ = enabled;
    });
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

NS_HWM_END
