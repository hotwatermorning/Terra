#include "LoggingStrategy.hpp"

#include "../misc/Either.hpp"

NS_HWM_BEGIN

using Strategy = Logger::LoggingStrategy;
using StrategyPtr = Logger::StrategyPtr;
using Error = Logger::Error;

Logger::LoggingStrategy::LoggingStrategy()
{}

Logger::LoggingStrategy::~LoggingStrategy()
{}

FileLoggingStrategy::FileLoggingStrategy(String path)
{
    path_ = path;
}

FileLoggingStrategy::~FileLoggingStrategy()
{
    Close();
}

bool FileLoggingStrategy::IsOpenedPermanently() const
{
    auto lock = lf_stream_.make_lock();
    return stream_.is_open();
}

String get_error_message()
{
    if(errno == 0) {
        return String();
    } else {
        return to_wstr(strerror(errno));
    }
}

template<class FileStreamType>
Either<Error, FileStreamType> create_file_stream(String path, std::ios::openmode mode)
{
    FileStreamType s;
#if defined(_MSC_VER)
    s.open(path, mode);
#else
    s.open(to_utf8(path), mode);
#endif
    
    if(s) {
        return std::move(s);
    } else {
        return Error(get_error_message());
    }
}

std::ios_base::openmode const kDefaultOpenMode = std::ios_base::app|std::ios_base::out;

Error FileLoggingStrategy::OpenPermanently()
{
    auto lock = lf_stream_.make_lock();
    if(stream_.is_open()) { return Error::NoError(); }
    
    auto result = create_file_stream<std::ofstream>(path_, kDefaultOpenMode);
    
    if(result.is_right()) {
        stream_ = std::move(result.right());
        return Error::NoError();
    } else {
        return result.left();
    }
}

Error FileLoggingStrategy::Close()
{
    auto lock = lf_stream_.make_lock();
    stream_.close();
    return Error(get_error_message());
}

void FileLoggingStrategy::OnAfterAssigned(Logger *logger)
{}

void FileLoggingStrategy::OnBeforeDeassigned(Logger *logger)
{}

Error FileLoggingStrategy::OutputLog(String const &message)
{
    auto lock = lf_stream_.make_lock();
    if(stream_.is_open()) {
        stream_ << message << std::endl;
        return Error(get_error_message());
    } else {
        lock.unlock();
        auto result = create_file_stream<std::ofstream>(path_, kDefaultOpenMode);
        if(result.is_right()) {
            result.right() << message << std::endl;
            if(result.right().fail()) {
                return Error(get_error_message());
            }
        } else {
            return result.left();
        }
    }
    
    return Error::NoError();
}

DebugConsoleLoggingStrategy::DebugConsoleLoggingStrategy()
{}

DebugConsoleLoggingStrategy::~DebugConsoleLoggingStrategy()
{}

Error DebugConsoleLoggingStrategy::OutputLog(String const &message)
{
#if defined(_MSC_VER)
    hwm::wdout << message << std::endl;
#else
    hwm::dout << to_utf8(message) << std::endl;
#endif
    
    return Error::NoError();
}

NS_HWM_END
