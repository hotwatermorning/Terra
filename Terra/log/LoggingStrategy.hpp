#pragma once

#include "./Logger.hpp"

#include <memory>

NS_HWM_BEGIN

class Logger::LoggingStrategy
{
protected:
    LoggingStrategy();
    
public:
    virtual
    ~LoggingStrategy();
    
    virtual
    void OnAfterAssigned(Logger *logger) {}
    
    virtual
    void OnBeforeDeassigned(Logger *logger) {}
    
    virtual
    Logger::Error OutputLog(String const &message) = 0;
};

class FileLoggingStrategy : public Logger::LoggingStrategy
{
public:
    //! Create FileLoggingStrategy object with the target file path.
    /*! the target file is not opened immediately.
     */
    FileLoggingStrategy(String path);
    ~FileLoggingStrategy();
    
    //! Returns whether the file has been opened permanently.
    /*! If this function returns false, the file will be opened and closed temporarily
     *  for each invocation of `Logger::OutputLog()`.
     */
    bool IsOpenedPermanently() const;
    
    //! open the file permanently with write permission.
    Logger::Error OpenPermanently();
    
    //! Close the target file if the file has been opened permanently.
    Logger::Error Close();
    
    void EnableRedirectionToDebugConsole(bool enable);
    bool IsEnabledRedirectionToDebugConsole() const;
    
    void OnAfterAssigned(Logger *logger) override;
    void OnBeforeDeassigned(Logger *logger) override;
    Logger::Error OutputLog(String const &message) override;
    
    
    //! Get file size limit.
    UInt64 GetFileSizeLimit() const;
    
    //! Set file size limit.
    /*! the log file is rotated and shrunk to 90% of this size every time the file is opened.
     */
    void SetFileSizeLimit(UInt64 size);
    
    static Logger::Error Rotate(String path, UInt64 size);
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

class DebugConsoleLoggingStrategy : public Logger::LoggingStrategy
{
public:
    DebugConsoleLoggingStrategy();
    ~DebugConsoleLoggingStrategy();
    
    Logger::Error OutputLog(String const &message) override;
};

NS_HWM_END
