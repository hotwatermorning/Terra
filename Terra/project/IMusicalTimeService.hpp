#pragma once
#include "./TimeInfoTypes.hpp"

NS_HWM_BEGIN

struct IMusicalTimeService
{
protected:
    IMusicalTimeService() {}
    
public:
    virtual
    ~IMusicalTimeService() {}
    
    virtual
    double GetSampleRate() const = 0;
    
    virtual
    Tick GetTpqn() const = 0;
    
    virtual
    double TickToSec(double tick) const = 0;
    
    virtual
    double SecToTick(double sec) const = 0;
    
    virtual
    double TickToSample(double tick) const = 0;
    
    virtual
    double SampleToTick(double sample) const = 0;
    
    virtual
    double SecToSample(double sec) const = 0;

    virtual
    double SampleToSec(double sample) const = 0;
    
    virtual
    double TickToPPQ(double tick) const = 0;
    
    virtual
    double PPQToTick(double ppq) const = 0;

    virtual
    MBT TickToMBT(Tick tick) const = 0;
    
    virtual
    Tick MBTToTick(MBT mbt) const = 0;
    
    virtual
    double GetTempoAt(double tick) const = 0;
    
    virtual
    Meter GetMeterAt(double tick) const = 0;
};

NS_HWM_END

