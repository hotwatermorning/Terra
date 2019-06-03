#pragma once

#include "./Transporter.hpp"

NS_HWM_BEGIN

//! This class traverse a frame samples which is required from the audio device.
//! The frame samples are split at loop range bounds.
class Transporter::Traverser
{
public:
    Traverser();
    Traverser(Traverser const &) = delete;
    Traverser & operator=(Traverser const &) = delete;
    Traverser(Traverser &&) = delete;
    Traverser & operator=(Traverser &&) = delete;
    
public:
    class ITraversalCallback
    {
    protected:
        ITraversalCallback() {}
    public:
        virtual
        ~ITraversalCallback() {}
        
        virtual
        void Process(TransportInfo const &info) = 0;
    };

    //! Progress the playback position of Transorter.
    /*! If loop is enabled and the playback frame intersects loop boundaries:
     *      - Split the frame length at loop boundary, and invoke
     *         ITraversalCallback::Process for each part of the frame.
     *      - If the playback position reaches to the end of the loop range,
     *        jump the playback position to the begin position of the loop range.
     */
    void Traverse(Transporter *tp, SampleCount length, ITraversalCallback *cb);
};

template<class F>
class TraversalCallback
:   public Transporter::Traverser::ITraversalCallback
{
public:
    explicit
    TraversalCallback(F f) : f_(std::forward<F>(f)) {}
    
    void Process(TransportInfo const &info) override
    {
        f_(info);
    }
    
    F f_;
};

//! Make ITraversalCallback object with a function object `f`.
/*! @tparam F is a function or a function object having a signature `void(TransportInfo const &)`
 */
template<class F>
TraversalCallback<F> MakeTraversalCallback(F f)
{
    return TraversalCallback<F>(std::forward<F>(f));
}

NS_HWM_END
