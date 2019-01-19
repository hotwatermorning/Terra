#pragma once

#include <cassert>
#include <algorithm>

NS_HWM_BEGIN

template<class ListenerType>
class ListenerService final
{
public:
    ListenerService()
    {}
    
    ~ListenerService()
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
    
private:
    std::vector<ListenerType *> listeners_;
};

struct ListenerBase
{
protected:
    ListenerBase() {}
    
public:
    virtual ~ListenerBase() {}
};

NS_HWM_END
