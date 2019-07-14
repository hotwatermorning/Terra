#include <atomic>
#include <fstream>

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

struct FileLoggingStrategy::Impl
{
    LockFactory lf_stream_;
    String path_;
    std::ofstream stream_;
    std::atomic<bool> redirect_to_debug_console_ = { true };
    std::atomic<UInt64> file_size_limit_ = { 20 * 1024 * 1024 };
};

FileLoggingStrategy::FileLoggingStrategy(String path)
:   pimpl_(std::make_unique<Impl>())
{
    pimpl_->path_ = path;
}

FileLoggingStrategy::~FileLoggingStrategy()
{
    Close();
}

bool FileLoggingStrategy::IsOpenedPermanently() const
{
    auto lock = pimpl_->lf_stream_.make_lock();
    return pimpl_->stream_.is_open();
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
    auto file = wxFileName::FileName(path);
    file.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    
    FileStreamType s;
    errno = 0;
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
    auto lock = pimpl_->lf_stream_.make_lock();
    if(pimpl_->stream_.is_open()) { return Error::NoError(); }
    
    if(auto err = Rotate(pimpl_->path_, GetFileSizeLimit() * 0.9)) {
        return err;
    }
    
    auto result = create_file_stream<std::ofstream>(pimpl_->path_, kDefaultOpenMode);
    
    if(result.is_right()) {
        pimpl_->stream_ = std::move(result.right());
        return Error::NoError();
    } else {
        return result.left();
    }
}

Error FileLoggingStrategy::Close()
{
    auto lock = pimpl_->lf_stream_.make_lock();
    errno = 0;
    pimpl_->stream_.close();
    return Error(get_error_message());
}

void FileLoggingStrategy::EnableRedirectionToDebugConsole(bool enable)
{
    pimpl_->redirect_to_debug_console_.store(enable);
}

bool FileLoggingStrategy::IsEnabledRedirectionToDebugConsole() const
{
    return pimpl_->redirect_to_debug_console_.load();
}

UInt64 FileLoggingStrategy::GetFileSizeLimit() const
{
    return pimpl_->file_size_limit_.load();
}

void FileLoggingStrategy::SetFileSizeLimit(UInt64 size)
{
    pimpl_->file_size_limit_.store(size);
}

Error FileLoggingStrategy::Rotate(String path, UInt64 size)
{
    if(wxFile::Exists(path) == false) {
        return Error::NoError();
    }
    
#if defined(_MSC_VER)
    std::ifstream src(path, std::ios::in|std::ios::out|std::ios::binary);
#else
    std::ifstream src(to_utf8(path), std::ios::in|std::ios::out|std::ios::binary);
#endif
    
    if(src.is_open() == false) { return Error(get_error_message()); }
    
    src.seekg(0, std::ios::end);
    auto pos = src.tellg();
    
    if(pos <= size) { return Error::NoError(); }
    
    wxFile tmp;
    
    auto tmp_file_path = wxFileName::CreateTempFileName("terra-log.tmp", &tmp);
    if(tmp.IsOpened() == false) { return Error(wxSysErrorMsg()); }
    
    src.seekg((UInt64)pos - size, std::ios::beg);
    std::vector<char> buf(1024 * 1024);
    
    for( ; ; ) {
        src.read(buf.data(), buf.size());
        if(src.fail() && !src.eof()) {
            // something wrong....
            return Error(get_error_message());
        }
        
        tmp.Write(buf.data(), src.gcount());
        if(src.eof()) { break; }
    }
    
    tmp.Flush();
    
    tmp.Close();
    src.close();
    
    if(wxRemoveFile(path) == false) {
        return Error(std::string("failed to remove the existing log file: ") + wxSysErrorMsg());
    }
    
    if(wxCopyFile(tmp_file_path, path) == false) {
        return Error(std::string("failed to move the rotated log file: ") + wxSysErrorMsg());
    }
    
    if(wxRemoveFile(tmp_file_path) == false) {
        std::cerr << "failed to cleanup the temporary file: " << wxSysErrorMsg() << std::endl;
        // do not treat as an error because rotation is completed.
    }
    
    return Error::NoError();
}

void FileLoggingStrategy::OnAfterAssigned(Logger *logger)
{}

void FileLoggingStrategy::OnBeforeDeassigned(Logger *logger)
{}

Error FileLoggingStrategy::OutputLog(String const &message)
{
    if(pimpl_->redirect_to_debug_console_.load()) {
#if defined(_MSC_VER)
        hwm::wdout << message << std::endl;
#else
        hwm::dout << to_utf8(message) << std::endl;
#endif
    }
    
    auto lock = pimpl_->lf_stream_.make_lock();
    if(pimpl_->stream_.is_open()) {
        errno = 0;
        pimpl_->stream_.clear();
        pimpl_->stream_ << to_utf8(message) << std::endl;
        if(pimpl_->stream_.fail()) {
            return Error(get_error_message());
        }
    } else {
        lock.unlock();
        Rotate(pimpl_->path_, GetFileSizeLimit() * 0.9);
        auto result = create_file_stream<std::ofstream>(pimpl_->path_, kDefaultOpenMode);
        if(result.is_right()) {
            errno = 0;
            result.right() << to_utf8(message) << std::endl;
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
