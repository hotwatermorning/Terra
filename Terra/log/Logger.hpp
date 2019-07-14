#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include "../misc/LockFactory.hpp"

NS_HWM_BEGIN

class Logger
{
public:
    class LoggingStrategy;
    using StrategyPtr = std::shared_ptr<LoggingStrategy>;
    
    //! Error class represents an error state of Logger class.
    /*! An Error object create with NoError factory method (or an Error object constructed with an empty string)
     *  represents successful state.
     */
    class Error
    {
    protected:
        Error() {}
        
    public:
        //! create an empty error object.
        static
        Error NoError() { return Error(); }
        
        //! create an error object with a message.
        explicit
        Error(String msg) : msg_(msg) {}
        
        //! return true iff this Error object has any error message.
        bool has_error() const { return msg_.empty() == false; }
        
        //! same as `has_error()`
        explicit operator bool() const { return has_error(); }
        
        //! get the error message.
        String message() const { return msg_; }
        
    private:
        String msg_;
    };
    
    //! Constructor
    /*! @post IsLoggingStarted() == false
     *  @post GetStrategy() is initialized with DebugConsoleLoggingStrategy.
     */
    Logger();
    
    //! Destructor
    ~Logger();
    
    //! Set logging levels with a list of level names.
    /*! @param levels [in] is treated as a ordered list where
     *  the first element is the most important level and
     *  the last element is the most detailed level.
     *  @note the most detailed active logging level will be reset
     *  as the last element of the new levels. (i.e. all levels will be active.)
     *  @pre IsLoggingStarted() == false
     */
    void SetLoggingLevels(std::vector<String> const &levels);
    
    template<class T>
    void SetLoggingLevels(std::initializer_list<T> const &levels) {
        SetLoggingLevels(std::vector<String>(levels.begin(), levels.end()));
    }
    
    //! Get logging levels.
    std::vector<String> GetLoggingLevels() const;
    
    //! Set the most detailed active logging level.
    /*! more detailed logging levels than this level are ignored in `OutputLog()`
     *  @pre `level` is found in the list returned from GetLoggingLevels().
     *  @pre IsLoggingStarted() == false
     */
    Error SetMostDetailedActiveLoggingLevel(String level);

    //! Returns the most detailed active logging level.
    /*! @return returns the current most detailed active logging level,
     *  or an empty string if GetLoggingLevels() is empty.
     */
    String GetMostDetailedActiveLoggingLevel() const;
    
    //! Returns true if the specified level is found in the logging level list and
    //! is equal or more important level as GetMostDetailedActiveLoggingLevel().
    bool IsActiveLoggingLevel(String level) const;
    
    //! Returns true if the specified level is found in the logging level list.
    bool IsValidLoggingLevel(String level) const;
    
    //! Set this logger started or stopped.
    /*! if a logger is stopped, any invocation of `OutputLog()` for any levels are ignored.
     */
    void StartLogging(bool start);

    //! Returns whether this logger is started.
    bool IsLoggingStarted() const;
    
    //! Set a new logging strategy.
    /*! @pre IsLoggingStarted() == false
     */
    void SetStrategy(StrategyPtr st);
    
    //! Remove the current strategy.
    /*! @return the pointer of the removed strategy if there, or nullptr otherwise.
     *  @pre IsLoggingStarted() == false
     */
    StrategyPtr RemoveStrategy();
    
    //! Get the current logging strategy.
    StrategyPtr GetStrategy() const;
    
    //! Output logging message which is obtained as a return value of `get_message`.
    /*! @tparam Func is a function object where the signature is `std::wstring(void)`.
     *  @param level [in] the target level.
     *  if this level is not active for this logger,
     *  `get_message` is never invoked and nothing will be outputted.
     *  In such case, the result value is Error::NoError().
     *  @return An Error object which may have an error message.
     *  @pre level is contained in the list of GetLoggingLevels().
     */
    template<class Func>
    Error OutputLog(String level, Func get_message)
    {
        auto lock = lf_logging_.make_lock();
        
        if(IsValidLoggingLevel(level) == false) { return Error(L"Invalid logging level is specified."); }
        
        if(IsLoggingStarted() == false) { return Error::NoError(); }
        
        if(IsActiveLoggingLevel(level) == false) {
            return Error::NoError();
        }
        return OutputLogImpl(level, get_message());
    }
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    LockFactory lf_logging_;
    
    Error OutputLogImpl(String level, String message);
};

NS_HWM_END
