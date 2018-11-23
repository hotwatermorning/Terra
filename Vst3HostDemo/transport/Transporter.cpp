#include "Transporter.hpp"

NS_HWM_BEGIN

Transporter::Transporter()
{}

Transporter::~Transporter()
{}

TransportInfo Transporter::GetCurrentState() const
{
    auto lock = lf_.make_lock();
    return transport_info_;
}

double GetPPQPos(TransportInfo const &info)
{
    double sec_pos = info.sample_pos_ / info.sample_rate_;
    double ppq_per_sec = info.tempo_ / 60.0;
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

NS_HWM_END
