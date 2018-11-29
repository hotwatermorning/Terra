#pragma once

#include "./Transporter.hpp"

NS_HWM_BEGIN

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

    //! 指定した長さだけトランスポート位置を進める。
    //! ループが有効な場合は、以下のように処理を行う。
    //!    * ループ境界ごとに処理を分割し、分割した領域ごとにITraversalCallback::Processを呼び出す。
    //!    * 処理がループ終端に達した場合は、次回の処理開始位置をループ先頭位置に巻き戻す。
    void Traverse(Transporter *tp, SampleCount length, ITraversalCallback *cb);
};

NS_HWM_END
