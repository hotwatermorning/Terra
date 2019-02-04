#pragma once

NS_HWM_BEGIN

class Meter
{
public:
    Meter() {}
    Meter(UInt16 numer, UInt16 denom)
    :   numer_(numer)
    ,   denom_(denom)
    {}
    
    UInt16 numer_ = 0;
    UInt16 denom_ = 0;
    
    Tick GetMeasureLength(Tick tpqn) const
    {
        return GetBeatLength(tpqn) * numer_;
    }
    
    Tick GetBeatLength(Tick tpqn) const
    {
        auto const whole_note = tpqn * 4;
        
        //! assert that the resolution of the tpqn is enough to represent the beat length.
        assert(whole_note == (whole_note / denom_) * denom_);
        
        return (whole_note / denom_);
    }
    
    bool operator==(Meter rhs) const
    {
        return (numer_ == rhs.numer_ && denom_ == rhs.denom_);
    }
    
    bool operator!=(Meter rhs) const
    {
        return !(*this == rhs);
    }
};

class MBT
{
public:
    MBT() {}
    MBT(UInt32 measure, UInt16 beat, UInt16 tick)
    :   measure_(measure)
    ,   beat_(beat)
    ,   tick_(tick)
    {}
    
    UInt32 measure_ = 0;
    UInt16 beat_ = 0;
    UInt16 tick_ = 0;
};

NS_HWM_END
