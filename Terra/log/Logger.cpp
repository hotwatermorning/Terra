#include <ctime>
#include <atomic>

#include "./Logger.hpp"
#include "./LoggingStrategy.hpp"
#include "../misc/Range.hpp"

NS_HWM_BEGIN

using Strategy = Logger::LoggingStrategy;
using StrategyPtr = Logger::StrategyPtr;
using Error = Logger::Error;

class Logger::Impl
{
public:
    LockFactory lf_;
    StrategyPtr st_;
    std::vector<String> levels_;
    //! an index to the most detailed active logging level.
    Int32 most_detailed_ = -1;
    std::atomic<bool> started_ = false;
};

#define MAKE_SURE_LOGGING_HAS_STOPPED() \
    do { \
        if(IsLoggingStarted()) { \
            throw std::runtime_error("do not call this function while logging has started."); \
        } \
    } while(0) \
/**/

Logger::Logger()
:   pimpl_(std::make_unique<Impl>())
{
    SetStrategy(std::make_shared<DebugConsoleLoggingStrategy>());
}

Logger::~Logger()
{
    StartLogging(false);
    RemoveStrategy();
}

void Logger::SetLoggingLevels(std::vector<String> const &levels)
{
    MAKE_SURE_LOGGING_HAS_STOPPED();
    
    pimpl_->levels_ = levels;
    pimpl_->most_detailed_ = pimpl_->levels_.size()-1;
}

std::vector<String> Logger::GetLoggingLevels() const
{
    return pimpl_->levels_;
}

Error Logger::SetMostDetailedActiveLoggingLevel(String level)
{
    MAKE_SURE_LOGGING_HAS_STOPPED();
    
    auto it = hwm::find(pimpl_->levels_, level);
    if(it == pimpl_->levels_.end()) {
        return Error(L"unknown logging level is specified.");
    }
    
    pimpl_->most_detailed_ = it - pimpl_->levels_.begin();
    return Error::NoError();
}

String Logger::GetMostDetailedActiveLoggingLevel() const
{
    if(pimpl_->levels_.empty()) {
        assert(pimpl_->most_detailed_ == -1);
        return String();
    } else {
        assert(0 <= pimpl_->most_detailed_ && pimpl_->most_detailed_ < pimpl_->levels_.size());
        return pimpl_->levels_[pimpl_->most_detailed_];
    }
}

bool Logger::IsActiveLoggingLevel(String level) const
{
    for(Int32 i = 0; i <= pimpl_->most_detailed_; ++i) {
        if(level == pimpl_->levels_[i]) { return true; }
    }
    
    return false;
}

bool Logger::IsValidLoggingLevel(String level) const
{
    return contains(pimpl_->levels_, level);
}

void Logger::StartLogging(bool start)
{
    //! Ensure the current logging operation has finished.
    auto lock = lf_logging_.make_lock();
    
    pimpl_->started_.store(start);
}

bool Logger::IsLoggingStarted() const
{
    return pimpl_->started_.load();
}

void Logger::SetStrategy(StrategyPtr st)
{
    MAKE_SURE_LOGGING_HAS_STOPPED();
    
    RemoveStrategy();

    pimpl_->st_ = st;
    if(pimpl_->st_) {
        pimpl_->st_->OnAfterAssigned(this);
    }
}

StrategyPtr Logger::RemoveStrategy()
{
    MAKE_SURE_LOGGING_HAS_STOPPED();
    
    if(pimpl_->st_) {
        pimpl_->st_->OnBeforeDeassigned(this);
    }
    
    return std::move(pimpl_->st_);
}

StrategyPtr Logger::GetStrategy() const
{
    return pimpl_->st_;
}

Error Logger::OutputLogImpl(String level, String message)
{
    std::string time_str;
    auto t = time(nullptr);
    struct tm ltime;
#if defined(_MSC_VER)
    auto error = localtime_s(&ltime, &t);
#else
    localtime_r(&t, &ltime);
    auto error = errno;
#endif
    if(error != 0) {
        time_str = std::string("(time not available: ") + strerror(error) + ")";
    } else {
        char buf[256] = {};
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S:%L %z", &ltime);
        time_str = buf;
    }
    
    String entry = L"[{}][{}] {}"_format(to_wstr(time_str),
                                         level,
                                         message);
    return GetStrategy()->OutputLog(entry);
}

NS_HWM_END
