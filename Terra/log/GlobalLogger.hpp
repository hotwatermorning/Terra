#pragma once

#include "./Logger.hpp"

NS_HWM_BEGIN

//! LoggerRef is a container to a logger.
/*! A Logger object managed by a LoggerRef is not
 *  destructed until the LoggerRef is destructed.
 */
class LoggerRef
{
public:
    struct Parameter;
    
    LoggerRef(Parameter p);
    ~LoggerRef();
    
    LoggerRef(LoggerRef const &) = delete;
    LoggerRef & operator=(LoggerRef const &) = delete;
    
    LoggerRef(LoggerRef &&);
    LoggerRef & operator=(LoggerRef &&);
    
    Logger * get() const noexcept { return logger_; }
    Logger * operator->() noexcept { return logger_; }
    
    bool is_valid() const noexcept { return logger_ != nullptr; }
    explicit operator bool() const noexcept { return is_valid(); }
    
    void reset();
    
private:
    Logger *logger_ = nullptr;
};

bool operator==(LoggerRef const &lhs, LoggerRef const &rhs) noexcept;
bool operator==(Logger const *lhs, LoggerRef const &rhs) noexcept;
bool operator==(LoggerRef const &lhs, Logger const *rhs) noexcept;
bool operator!=(LoggerRef const &lhs, LoggerRef const &rhs) noexcept;
bool operator!=(Logger const *lhs, LoggerRef const &rhs) noexcept;
bool operator!=(LoggerRef const &lhs, Logger const *rhs) noexcept;

//! Get global logger
/*! @return LoggerRef object to the global logger.
 *  If there's no initialized global logger, returns an empty LoggerRef.
 *  @note Do not attempt to store LoggerRef object for long time use,
 *  otherwise ReplaceGlobalLogger() will be blocked forever until the LoggerRef is destructed.
 */
LoggerRef GetGlobalLogger();

//! Initialize global logger with the specified logger.
/*! @note Invocation of this function will be blocked while at least one LoggerRef object is still in use.
 *  @return a pointer to the previous global logger.
 */
std::unique_ptr<Logger> ReplaceGlobalLogger(std::unique_ptr<Logger> logger);

NS_HWM_END
