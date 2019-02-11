#pragma once

#include <cassert>
#include <algorithm>

NS_HWM_BEGIN

template<class ListenerType>
class IListenerService
{
protected:
    IListenerService()
    {}
    
public:
    virtual
    ~IListenerService()
    {
        assert(listeners_.empty());
    }
    
    void AddListener(ListenerType *li)
    {
        auto found = std::find(listeners_.begin(), listeners_.end(), li);
        if(found == listeners_.end()) {
            listeners_.push_back(li);
        }
    }
    
    void RemoveListener(ListenerType const *li)
    {
        auto found = std::find(listeners_.begin(), listeners_.end(), li);
        if(found != listeners_.end()) {
            listeners_.erase(found);
        }
    }
    
protected:
    std::vector<ListenerType *> listeners_;
};

template<class ListenerType>
class ListenerService
:   public IListenerService<ListenerType>
{
private:
    using IListenerService<ListenerType>::listeners_;
    
public:
    ListenerService()
    {}
    
    template<class F>
    void Invoke(F f) {
        auto tmp = listeners_; // コールバックの中で、Add/Removeを呼び出せるように
        std::for_each(tmp.begin(), tmp.end(), f);
    };
    
    template<class F>
    void InvokeReversed(F f) {
        auto tmp = listeners_; // コールバックの中で、Add/Removeを呼び出せるように
        std::for_each(tmp.rbegin(), tmp.rend(), f);
    };
};

struct IListenerBase
{
protected:
    IListenerBase() {}
    
public:
    virtual ~IListenerBase() {}
};

template<class ListenerType>
class ScopedListenerRegister final
{
public:
    ScopedListenerRegister()
    {}
    
    ScopedListenerRegister(IListenerService<ListenerType> &ls, ListenerType *li)
    :   ls_(&ls)
    ,   li_(li)
    {
        assert(ls_);
        assert(li_);
        
        ls_->AddListener(li_);
    }
    
    ScopedListenerRegister(ScopedListenerRegister const &) = delete;
    ScopedListenerRegister & operator=(ScopedListenerRegister const &) = delete;
    
    ScopedListenerRegister(ScopedListenerRegister&& rhs)
    {
        ls_ = rhs.ls_;
        li_ = rhs.li_;
        rhs.ls_ = nullptr;
        rhs.li_ = nullptr;
    }
    
    ScopedListenerRegister & operator=(ScopedListenerRegister&& rhs)
    {
        reset();
        ls_ = rhs.ls_;
        li_ = rhs.li_;
        rhs.ls_ = nullptr;
        rhs.li_ = nullptr;
        return *this;
    }
    
    ~ScopedListenerRegister()
    {
        reset();
    }
    
    //! @return true if registered.
    explicit operator bool() const { return li_; }
    
    void reset()
    {
        assert((ls_ && li_) || (!ls_ && !li_));

        if(li_) {
            ls_->RemoveListener(li_);
            li_ = nullptr;
            ls_ = nullptr;
        }
    }
    
    void reset(IListenerService<ListenerType> &ls, ListenerType *li)
    {
        *this = ScopedListenerRegister(ls, li);
    }
    
private:
    IListenerService<ListenerType> *ls_ = nullptr;
    ListenerType *li_ = nullptr;
};

NS_HWM_END
