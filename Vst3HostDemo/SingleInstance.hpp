#pragma once

#include <atomic>
#include <cassert>

//! @file

NS_HWM_BEGIN

//! クラスのインスタンスの作成個数をひとつだけに制限する仕組みを提供するクラス
template<class Derived>
class SingleInstance
{
public:
    typedef SingleInstance<Derived> this_type;
    
protected:
    SingleInstance()
    {
        this_type *expected = nullptr;
        bool const replaced = single_instance_.compare_exchange_strong(expected, this);
        assert(replaced);
    }
    
public:
    virtual
    ~SingleInstance()
    {
        single_instance_ = nullptr;
    }
    
    //! 唯一のインスタンスを返す。
    /*! もしインスタンスが作成されていなければnullptrが返る
     */
    static
    Derived * GetInstance() { return static_cast<Derived *>(single_instance_.load()); }
    
private:
    static std::atomic<this_type *> single_instance_;
};

template<class Derived>
std::atomic<SingleInstance<Derived> *> SingleInstance<Derived>::single_instance_;

NS_HWM_END
