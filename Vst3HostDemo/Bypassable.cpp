#include "Bypassable.hpp"
#include <thread>
#include <chrono>

NS_HWM_BEGIN

namespace {
    bool is_guarded(std::uint32_t status) { return status % 2 == 1; }
}

BypassFlag::BypassFlag()
{
    status_.store(0);
}

BypassFlag::~BypassFlag()
{
    assert(status_.load() == 0);
}

bool BypassFlag::BeginBypassGuard()
{
    // ビジーウェイトを試行する回数
    // 他のスレッドでbypassをリクエストして処理を行っている場合、
    // kLimitToRetryBusyWaitで指定された回数だけビジーウェイトを行い、
    // その間に他のスレッドの処理が完了するのを待つ。
    static std::uint32_t const kLimitToRetryBusyWait = 1;
    
    for(std::uint32_t i = 0; i < kLimitToRetryBusyWait; ) {
        auto current = status_.load();
        assert(is_guarded(current) == false);
        
        if(current != 0) {
            ++i;
            continue;
        }
        
        // Spurious Failureが発生してしまうと、そのフレームの再生処理が飛ばされてしまうので、
        // strong版を使用して確実にCASを実行する
        bool const successful = status_.compare_exchange_strong(current, 1);
        
        if(successful) {
            return true;
        }
        
        // CASに失敗した場合は、もう一度ループを再開
        // これはCAS前と状態が変わっているという状況なので、
        // iをインクリメントせずに単純にもう一度試行する
    }
    
    return false;
}

void BypassFlag::EndBypassGuard()
{
    for( ; ; ) {
        auto current = status_.load();
        assert(is_guarded(current) == true);
        
        // 最下位ビットを0にする
        bool const successful = status_.compare_exchange_weak(current,
                                                              current & 0xFFFFFFFE);
        
        if(successful) {
            return;
        }
    }
}

bool BypassFlag::IsBypassGuardEnabled() const
{
    return is_guarded(status_.load());
}

bool BypassFlag::RequestToBypass()
{
    std::uint32_t current = 0;
    for( ; ; ) {
        current = status_.load();
        bool const successful = status_.compare_exchange_weak(current,
                                                              current + 2);
        
        if(successful) {
            break;
        }
    }
    
    return is_guarded(current) == false;
}

void BypassFlag::WaitToApplyBypassing()
{
    for(std::uint32_t i = 0; ; ++i) {
        auto current = status_.load();

        assert(current >= 2);
        
        if(is_guarded(current) == false) {
            return;
        }
        
        if(i < 30) {
            continue;
        } else if(i < 100) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void BypassFlag::ReleaseBypassRequest()
{
    for( ; ; ) {
        auto current = status_.load();
        assert(current >= 2);
 
        // 現在のリクエストカウントを減らす
        bool const successful = status_.compare_exchange_weak(current,
                                                              current - 2);
        
        if(successful) {
            return;
        }
    }
}

void BypassFlag::WaitToFinishBypassing()
{
    for(std::uint32_t i = 0; ; ++i) {
        auto current = status_.load();

        if(current < 2) {
            return;
        }
        
        if(i < 30) {
            continue;
        } else if(i < 100) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

ScopedBypassRequest::ScopedBypassRequest()
: by_(nullptr)
{}

ScopedBypassRequest::ScopedBypassRequest(BypassFlag &by, bool should_wait)
{
    auto succeeded = by.RequestToBypass();
    if(!succeeded && should_wait) {
        by.WaitToApplyBypassing();
        succeeded = true;
    }
    
    by_ = (succeeded ? &by : nullptr);
}

ScopedBypassRequest::~ScopedBypassRequest()
{
    reset();
}

ScopedBypassRequest::operator bool() const noexcept
{
    return by_;
}

bool ScopedBypassRequest::is_bypassing() const noexcept
{
    return by_;
}

ScopedBypassRequest::ScopedBypassRequest(ScopedBypassRequest &&rhs)
{
    by_ = rhs.by_;
    rhs.by_ = nullptr;
}

ScopedBypassRequest & ScopedBypassRequest::operator=(ScopedBypassRequest &&rhs)
{
    reset();
    
    by_ = rhs.by_;
    rhs.by_ = nullptr;
    
    return *this;
}

void ScopedBypassRequest::reset()
{
    if(by_) {
        by_->ReleaseBypassRequest();
        by_ = nullptr;
    }
}

ScopedBypassRequest MakeScopedBypassRequest(BypassFlag &by, bool should_wait)
{
    return ScopedBypassRequest(by, should_wait);
}

ScopedBypassGuard::ScopedBypassGuard()
:   by_(nullptr)
{}

ScopedBypassGuard::ScopedBypassGuard(BypassFlag &by)
:   by_(nullptr)
{
    if(by.BeginBypassGuard()) {
        by_ = &by;
    }
}

ScopedBypassGuard::~ScopedBypassGuard()
{
    reset();
}

ScopedBypassGuard::operator bool() const noexcept
{
    return is_guarded();
}

bool ScopedBypassGuard::is_guarded() const noexcept
{
    return by_;
}

ScopedBypassGuard::ScopedBypassGuard(ScopedBypassGuard &&rhs)
{
    by_ = rhs.by_;
    rhs.by_ = nullptr;
}

ScopedBypassGuard & ScopedBypassGuard::operator=(ScopedBypassGuard &&rhs)
{
    reset();
    
    by_ = rhs.by_;
    rhs.by_ = nullptr;
    
    return *this;
}

void ScopedBypassGuard::reset()
{
    if(by_) {
        by_->EndBypassGuard();
        by_ = nullptr;
    }
}

ScopedBypassGuard MakeScopedBypassGuard(BypassFlag &by)
{
    return ScopedBypassGuard(by);
}

NS_HWM_END
