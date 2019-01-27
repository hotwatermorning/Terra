#pragma once

#include <functional>

NS_HWM_BEGIN

namespace detail {
    
template <typename F>
class ScopeExit final
{
public:
    ScopeExit(F&& on_exit)
    : on_exit_(std::forward<F>(on_exit))
    , initialized_(true)
    {
    }
    
    ScopeExit(ScopeExit<F>&& rhs)
    :   on_exit_(std::move(rhs.on_exit_))
    {
        rhs.initialized_ = false;
        initialized_ = true;
    }
    
    ScopeExit<F>& operator=(ScopeExit<F>&& rhs)
    {
        on_exit_ = std::move(rhs.on_exit_);
        rhs.initialized_ = false;
        initialized_ = true;
    }
    
    ~ScopeExit() { if(initialized_) { on_exit_(); } }
    
private:
    F on_exit_;
    bool initialized_;
};

template <typename F>
ScopeExit<F> MakeScopeExit(F&& f) { return ScopeExit<F>(std::forward<F>(f)); }
    
} // namespace detail

NS_HWM_END

#define HWM_SCOPE_EXIT_CAT(x, y) HWM_SCOPE_EXIT_CAT_I(x, y)
#define HWM_SCOPE_EXIT_CAT_I(x, y) x##y
#define HWM_SCOPE_EXIT(...) HWM_SCOPE_EXIT_CAT(auto hwm_scope_exit_, __LINE__) \
= hwm::detail::MakeScopeExit(__VA_ARGS__)
