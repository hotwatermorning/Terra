#pragma once

#include "../project/TimeInfoTypes.hpp"

NS_HWM_BEGIN

class Duration;

class TimePoint
{
public:
    SampleCount sample_ = 0;
    double tick_ = 0;
    double sec_ = 0;
    
    TimePoint & operator+=(Duration const &);
    TimePoint & operator-=(Duration const &);
    TimePoint operator+(Duration const &) const;
    TimePoint operator-(Duration const &) const;
    Duration operator+(TimePoint const &) const;
    Duration operator-(TimePoint const &) const;
    
    bool operator==(TimePoint const &rhs) const
    {
        auto const to_tuple = [](TimePoint const &x) {
            return std::tie(x.sample_, x.tick_, x.sec_);
        };
        
        return to_tuple(*this) == to_tuple(rhs);
    }
    
    bool operator!=(TimePoint const &rhs) const
    {
        return !(*this == rhs);
    }
};

class Duration
{
public:
    SampleCount sample_ = 0;
    double tick_ = 0;
    double sec_ = 0;
    
    Duration & operator+=(Duration const &);
    Duration & operator-=(Duration const &);
    Duration operator+(Duration const &) const;
    Duration operator-(Duration const &) const;
    TimePoint operator+(TimePoint const &) const;
    TimePoint operator-(TimePoint const &) const;
    
    bool operator==(Duration const &rhs) const
    {
        auto const to_tuple = [](Duration const &x) {
            return std::tie(x.sample_, x.tick_, x.sec_);
        };
        
        return to_tuple(*this) == to_tuple(rhs);
    }
    
    bool operator!=(Duration const &rhs) const
    {
        return !(*this == rhs);
    }
};

class TimeRange
{
public:
    TimeRange() {}
    TimeRange(TimePoint begin, TimePoint end)
    :   begin_(begin)
    ,   end_(end)
    ,   duration_(end - begin)
    {}
    
    TimeRange(TimePoint begin, Duration duration)
    :   begin_(begin)
    ,   end_(begin + duration)
    ,   duration_(duration)
    {}
    
    TimePoint begin_;
    TimePoint end_;
    Duration duration_;
    
    bool operator==(TimeRange const &rhs) const
    {
        auto const to_tuple = [](TimeRange const &x) {
            return std::tie(x.begin_, x.end_, x.duration_);
        };
        
        return to_tuple(*this) == to_tuple(rhs);
    }
    
    bool operator!=(TimeRange const &rhs) const
    {
        return !(*this == rhs);
    }
};

class TransportInfo {
public:
    double sample_rate_ = 0;
    Tick tpqn_ = 0;

    TimeRange play_;
    TimeRange loop_;

    bool playing_ = 0;
    bool loop_enabled_ = false;
    double tempo_ = 120.0;
    Meter meter_ = Meter(4, 4);
    
    bool IsLooping() const {
        return loop_enabled_ && (loop_.duration_.sample_ > 0);
    }
};

NS_HWM_END
