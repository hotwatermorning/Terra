#include "Transporter.hpp"

NS_HWM_BEGIN

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

double GetPPQPos(TransportInfo const &info)
{
    double sec_pos = info.sample_pos_ / info.sample_rate_;
    double ppq_per_sec = info.tempo_ / 60.0;
    return sec_pos * ppq_per_sec;
}

template<class F>
std::pair<TransportInfo, TransportInfo> Alter(LockFactory & lf,
                                              TransportInfo &info,
                                              F f)
{
    std::pair<TransportInfo, TransportInfo> ret;
    
    auto lock = lf.make_lock();
    ret.first = info;
    f(info);
    ret.second = info;
    lock.unlock();
    
    return ret;
}

void Transporter::MoveTo(SampleCount pos)
{
    auto pair = Alter(lf_,
                      transport_info_,
                      [&](TransportInfo &info) {
                          transport_info_.sample_pos_ = pos;
                          transport_info_.last_moved_pos_ = pos;
                          transport_info_.ppq_pos_ = GetPPQPos(transport_info_);
                      });
    
    listeners_.Invoke([&pair](auto *li) {
        li->OnChanged(pair.first, pair.second);
    });
}

bool Transporter::IsPlaying() const {
    auto lock = lf_.make_lock();
    return transport_info_.playing_;
}

void Transporter::SetPlaying(bool is_playing)
{
    auto pair = Alter(lf_,
                      transport_info_,
                      [&](TransportInfo &info) {
                          transport_info_.playing_ = is_playing;
                      });
    
    listeners_.Invoke([&pair](auto *li) {
        li->OnChanged(pair.first, pair.second);
    });
}

void Transporter::SetLoopRange(SampleCount begin, SampleCount end)
{
    assert(0 <= begin);
    assert(begin <= end);
    
    auto pair = Alter(lf_,
                      transport_info_,
                      [&](TransportInfo &info) {
                          transport_info_.loop_begin_ = begin;
                          transport_info_.loop_end_ = end;
                      });
    
    listeners_.Invoke([&pair](auto *li) {
        li->OnChanged(pair.first, pair.second);
    });
}

void Transporter::SetLoopEnabled(bool enabled)
{
    auto pair = Alter(lf_,
                      transport_info_,
                      [&](TransportInfo &info) {
                          transport_info_.loop_enabled_ = enabled;
                      });
    
    listeners_.Invoke([&pair](auto *li) {
        li->OnChanged(pair.first, pair.second);
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
