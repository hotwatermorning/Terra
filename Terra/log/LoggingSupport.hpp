#pragma once

#include "./GlobalLogger.hpp"

NS_HWM_BEGIN

//! Initialize the global logger with the default logging levels (Error, Warn, Info, Debug).
void InitializeDefaultGlobalLogger();

bool IsEnabledErrorCheckAssertionForLoggingMacros();
void EnableErrorCheckAssertionForLoggingMacros(bool enable);

template<class Stream>
struct LoggingStreamWrapper
{
    LoggingStreamWrapper(Stream &s)
    :   s_(&s)
    {}
    
    Stream & base() { return static_cast<Stream &>(*s_); }
    
public:
    static constexpr bool false_value_ = false;
    
private:
    Stream *s_;
};

template<class Stream, class T>
LoggingStreamWrapper<Stream> &
operator<<(LoggingStreamWrapper<Stream> &os, T arg)
{
    os.base() << arg;
    return os;
}

template<class Stream>
LoggingStreamWrapper<Stream> &
operator<<(LoggingStreamWrapper<Stream> &os, std::wostream & (*arg)(std::wostream &))
{
    (*arg)(os.base());
    return os;
}

//! マルチバイト文字列は受け付けない
template<class Stream>
LoggingStreamWrapper<Stream> & operator<<(LoggingStreamWrapper<Stream> &os, std::string const &rhs)
{
    static_assert(LoggingStreamWrapper<Stream>::false_value_, "multi byte string is not allowed");
    return os;
}

//! マルチバイト文字列は受け付けない
template<class Stream>
LoggingStreamWrapper<Stream> & operator<<(LoggingStreamWrapper<Stream> &os, char const *rhs)
{
    static_assert(LoggingStreamWrapper<Stream>::false_value_, "multi byte string is not allowed");
    return os;
}

// logging macro.
#define TERRA_LOG(level, ...) \
do { \
    if(auto global_logger_ref ## __LINE__ = GetGlobalLogger()) { \
        auto error = global_logger_ref ## __LINE__ ->OutputLog(level, [&] { \
            std::wstringstream ss; \
            LoggingStreamWrapper<std::wstringstream> wss(ss); \
            wss << __VA_ARGS__; \
            return ss.str(); \
        }); \
        if(IsEnabledErrorCheckAssertionForLoggingMacros()) { \
            assert(error.has_error() == false); \
        } \
    } \
} while(0) \
/* */

// dedicated logging macros for the default logging levels.

#define TERRA_ERROR_LOG(...) TERRA_LOG(L"Error", __VA_ARGS__)
#define TERRA_WARN_LOG(...) TERRA_LOG(L"Warn", __VA_ARGS__)
#define TERRA_INFO_LOG(...) TERRA_LOG(L"Info", __VA_ARGS__)
#define TERRA_DEBUG_LOG(...) TERRA_LOG(L"Debug", __VA_ARGS__)

NS_HWM_END
