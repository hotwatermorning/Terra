#pragma once

NS_HWM_BEGIN

//! Get the test executable's path.
String GetExecutablePath();

//! Get the root path of the temporary directories used for some test cases for any purpose.
String GetTestTemporaryRootPath();

//! ScopedTemporaryDirectoryProvider creates a temporary
//! directory under GetTestTemporaryRootPath(),
//! and delete it before destructed.
class [[nodiscard]] ScopedTemporaryDirectoryProvider
{
public:
    //! Constructor.
    /*! @throw std::runtime_error if failed to cleanup or create the directory.
     */
    ScopedTemporaryDirectoryProvider(String dir_name);
    ~ScopedTemporaryDirectoryProvider();
    
    ScopedTemporaryDirectoryProvider(ScopedTemporaryDirectoryProvider const &) = delete;
    ScopedTemporaryDirectoryProvider & operator=(ScopedTemporaryDirectoryProvider const &) = delete;
    
    //! Move constructor.
    /*! The moved-from object will release the ownership to the path.
     */
    ScopedTemporaryDirectoryProvider(ScopedTemporaryDirectoryProvider &&rhs);

    //! Move assignment operator.
    /*! The moved-from object will release the ownership to the path.
     */
    ScopedTemporaryDirectoryProvider & operator=(ScopedTemporaryDirectoryProvider &&rhs);
    
    //! Delete the directory, and forget the path.
    void reset();
    
    //! Returns full directory path.
    String GetPath() const;
    
private:
    String dir_name_;
};

NS_HWM_END
