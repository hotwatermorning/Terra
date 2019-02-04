#include "TransportInfo.hpp"

NS_HWM_BEGIN

TimePoint & TimePoint::operator+=(Duration const &rhs)
{
    sample_ += rhs.sample_;
    tick_ += rhs.tick_;
    sec_ += rhs.sec_;
    return *this;
}

TimePoint & TimePoint::operator-=(Duration const &rhs)
{
    sample_ -= rhs.sample_;
    tick_ -= rhs.tick_;
    sec_ -= rhs.sec_;
    return *this;
}

TimePoint TimePoint::operator+(Duration const &rhs) const
{
    auto tmp = *this;
    return tmp += rhs;
}

TimePoint TimePoint::operator-(Duration const &rhs) const
{
    auto tmp = *this;
    return tmp -= rhs;
}

Duration TimePoint::operator+(TimePoint const &rhs) const
{
    Duration tmp;
    tmp.sample_ = sample_ + rhs.sample_;
    tmp.tick_ = tick_ + rhs.tick_;
    tmp.sec_ = sec_ + rhs.sec_;
    return tmp;
}

Duration TimePoint::operator-(TimePoint const &rhs) const
{
    Duration tmp;
    tmp.sample_ = sample_ - rhs.sample_;
    tmp.tick_ = tick_ - rhs.tick_;
    tmp.sec_ = sec_ - rhs.sec_;
    return tmp;
}

Duration & Duration::operator+=(Duration const &rhs)
{
    sample_ += rhs.sample_;
    tick_ += rhs.tick_;
    sec_ += rhs.sec_;
    return *this;
}

Duration & Duration::operator-=(Duration const &rhs)
{
    sample_ -= rhs.sample_;
    tick_ -= rhs.tick_;
    sec_ -= rhs.sec_;
    return *this;
}

Duration Duration::operator+(Duration const &rhs) const
{
    auto tmp = *this;
    return tmp += rhs;
}

Duration Duration::operator-(Duration const &rhs) const
{
    auto tmp = *this;
    return tmp -= rhs;
}

TimePoint Duration::operator+(TimePoint const &rhs) const
{
    return rhs + *this;
}

TimePoint Duration::operator-(TimePoint const &rhs) const
{
    return rhs - *this;
}

NS_HWM_END
