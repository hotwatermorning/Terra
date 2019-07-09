#include "LoggingSupport.hpp"

#include <condition_variable>
#include "../misc/LockFactory.hpp"

NS_HWM_BEGIN

namespace {
    LockFactory g_lf_;
    std::condition_variable g_cond_;
    Int32 g_used_count_;
    std::unique_ptr<Logger> g_logger_;
}

struct LoggerRef::Parameter
{
    Parameter(Logger *logger = nullptr)
    : logger_(logger)
    {}
    
    Logger *logger_;
};

LoggerRef::LoggerRef(Parameter p)
{
    logger_ = p.logger_;
}

LoggerRef::~LoggerRef()
{
    reset();
}

LoggerRef::LoggerRef(LoggerRef &&rhs)
{
    logger_ = rhs.logger_;
    rhs.logger_ = nullptr;
}

LoggerRef & LoggerRef::operator=(LoggerRef &&rhs)
{
    reset();
    
    logger_ = rhs.logger_;
    rhs.logger_ = nullptr;
    
    return *this;
}

void LoggerRef::reset()
{
    if(!logger_) { return; }
    
    auto lock = g_lf_.make_lock();
    
    assert(g_used_count_ != 0);
    
    g_used_count_ -= 1;
    if(g_used_count_ == 0) {
        g_cond_.notify_one();
    }
    logger_ = nullptr;
}

bool operator==(LoggerRef const &lhs, LoggerRef const &rhs) noexcept
{
    return lhs.get() == rhs.get();
}

bool operator==(Logger const *lhs, LoggerRef const &rhs) noexcept
{
    return lhs == rhs.get();
}

bool operator==(LoggerRef const &lhs, Logger const *rhs) noexcept
{
    return lhs.get() == rhs;
}

bool operator!=(LoggerRef const &lhs, LoggerRef const &rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator!=(Logger const *lhs, LoggerRef const &rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator!=(LoggerRef const &lhs, Logger const *rhs) noexcept
{
    return !(lhs == rhs);
}

LoggerRef GetGlobalLogger()
{
    auto lock = g_lf_.make_lock();
    
    if(g_logger_) {
        g_used_count_ += 1;
    }
    
    return LoggerRef(LoggerRef::Parameter(g_logger_.get()));
}

std::unique_ptr<Logger> ReplaceGlobalLogger(std::unique_ptr<Logger> new_logger)
{
    auto lock = g_lf_.make_lock();
    
    if(g_used_count_ != 0) {
        g_cond_.wait(lock, [] { return g_used_count_ == 0; });
    }
    
    auto prev = std::move(g_logger_);
    g_logger_ = std::move(new_logger);
    return prev;
}

NS_HWM_END
