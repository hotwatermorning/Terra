#pragma once

#include <utility>
#include <mutex>
#include "../misc/LockFactory.hpp"
#include "TransportInfo.hpp"

NS_HWM_BEGIN

class Transporter
{
public:
    class Traverser;
    
    Transporter();
    ~Transporter();
    
    TransportInfo GetCurrentState() const;
    
    void MoveTo(SampleCount pos);
    bool IsPlaying() const;
    void SetPlaying(bool is_playing);
    void SetLoopRange(SampleCount begin, SampleCount end);
    void SetLoopEnabled(bool enabled);
    std::pair<SampleCount, SampleCount> GetLoopRange() const;
    bool IsLoopEnabled() const;

private:
    LockFactory lf_;    
    TransportInfo transport_info_;
};

NS_HWM_END
