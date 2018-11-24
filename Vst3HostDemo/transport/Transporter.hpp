#pragma once

#include <utility>
#include <mutex>
#include "../misc/LockFactory.hpp"
#include "TransportInfo.hpp"
#include "../misc/ListenerService.hpp"

NS_HWM_BEGIN

class Transporter
{
public:
    class Traverser;
    
    Transporter();
    ~Transporter();
    
    TransportInfo GetCurrentState() const;
    
    class ITransportStateListener
    {
    protected:
        ITransportStateListener() {}
        
    public:
        virtual
        ~ITransportStateListener() {}
        
        virtual
        void OnChanged(TransportInfo const &old_state,
                       TransportInfo const &new_state) = 0;
    };
    
    void AddListener(ITransportStateListener *li);
    void RemoveListener(ITransportStateListener const *li);
    
    void MoveTo(SampleCount pos);
    //! jump 1 measure before
    void Rewind();
    //! jump 1 measure after
    void FastForward();
    bool IsPlaying() const;
    void SetPlaying(bool is_playing);
    void SetStop();
    void SetLoopRange(SampleCount begin, SampleCount end);
    void SetLoopEnabled(bool enabled);
    std::pair<SampleCount, SampleCount> GetLoopRange() const;
    bool IsLoopEnabled() const;

private:
    LockFactory lf_;    
    TransportInfo transport_info_;
    ListenerService<ITransportStateListener> listeners_;

    template<class F>
    void AlterTransportInfo(F f);
};

NS_HWM_END
