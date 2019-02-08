#pragma once

#include <utility>
#include <mutex>
#include "../misc/LockFactory.hpp"
#include "TransportInfo.hpp"
#include "../misc/ListenerService.hpp"
#include "../project/IMusicalTimeService.hpp"

NS_HWM_BEGIN

//! Represent the current playing back position, and
//! provide some functions to operate playing back status.
class Transporter
{
public:
    class Traverser;
    
    Transporter(IMusicalTimeService const *tc);
    ~Transporter();
    
    IMusicalTimeService const * GetMusicalTimeService() const;
    
    TransportInfo GetCurrentState() const;
    
    bool IsPlaying() const;
    TimeRange GetLoopRange() const;
    bool IsLoopEnabled() const;
    
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
    /*! if the current playing back position is in a range which
     *  starts from a measure and has tolerance length,
     *  then this function rewinds the playing back position to the previous measure,
     *  otherwise rewinds the playing back position to the begin of the measure.
     */
    void Rewind(Tick tolerance = 0);
    
    //! jump 1 measure after
    void FastForward();
    
    //! Set playing back status
    void SetPlaying(bool is_playing);
    //! Stop and move to the last moved position.
    void SetStop();
    //! Set loop range with a pair of sample positions.
    void SetLoopRange(SampleCount begin, SampleCount end);
    //! Set the loop status.
    /*! @note looped playing back is disabled when the loop range is invalid or empty
     *  even if the loop status has beed enabled.
     */
    void SetLoopEnabled(bool enabled);
    
    //! Generate a TimePoint for the specified sample position.
    TimePoint SampleToTimePoint(SampleCount sample) const;
    
    TimePoint GetLastMovedPos() const;

private:
    IMusicalTimeService const *mt_;
    LockFactory lf_;    
    TransportInfo transport_info_;
    ListenerService<ITransportStateListener> listeners_;
    TimePoint last_moved_pos_;

    template<class F>
    void AlterTransportInfo(F f);
};

NS_HWM_END
