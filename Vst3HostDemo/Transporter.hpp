#pragma once

#include <utility>
#include <mutex>
#include "LockFactory.hpp"
#include "TransportInfo.hpp"

NS_HWM_BEGIN

class Transporter
{
public:
    SampleCount GetCurrentPos() const;
    void MoveTo(SampleCount pos);
    bool IsPlaying() const;
    void SetPlaying(bool is_playing);
    void SetLoopRange(SampleCount begin, SampleCount end);
    void SetLoopEnabled(bool enabled);
    std::pair<SampleCount, SampleCount> GetLoopRange() const;
    bool IsLoopEnabled() const;
    
    class ITraversalCallback
    {
    protected:
        ITraversalCallback() {}
    public:
        virtual
        ~ITraversalCallback() {}
    
        virtual
        void Process(TransportInfo const &info, SampleCount length) = 0;
    };
    
    //! 指定した長さだけトランスポート位置を進める。
    //! ループが有効な場合は、以下のように処理を行う。
    //!    * ループ境界ごとに処理を分割し、分割した領域ごとにITraversalCallback::Processを呼び出す。
    //!    * 処理がループ終端に達した場合は、次回の処理開始位置をループ先頭位置に巻き戻す。
    void Traverse(SampleCount length, ITraversalCallback *cb);

    LockFactory lf_;    
    TransportInfo transport_info_;
};

NS_HWM_END
