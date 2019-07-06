#include "PathUtil.hpp"

#include <wx/stdpaths.h>
#include <wx/filename.h>

#include "../log/LoggingSupport.hpp"

NS_HWM_BEGIN

String GetExecutablePath()
{
    return wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
}

String GetTestTemporaryRootPath()
{
    auto filename = wxFileName::FileName(wxStandardPaths::Get().GetExecutablePath());
    filename.AppendDir(L"Temporary");
    return filename.GetPath().ToStdWstring();
}

ScopedTemporaryDirectoryProvider::ScopedTemporaryDirectoryProvider(String dir_name)
:   dir_name_(dir_name)
{
    assert(dir_name_.empty() == false);
    
    wxFileName path = wxFileName::DirName(GetTestTemporaryRootPath());
    path.AppendDir(dir_name_);
    if(path.Exists()) {
        if(path.Rmdir(wxPATH_RMDIR_RECURSIVE) == false) {
            throw std::runtime_error("failed to remove the temporary directory: " + to_utf8(dir_name_));
        }
    }
    if(path.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) == false) {
        throw std::runtime_error("failed to prepare the temporary directory: " + to_utf8(dir_name_));
    }
}

ScopedTemporaryDirectoryProvider::~ScopedTemporaryDirectoryProvider()
{
    reset();
}

ScopedTemporaryDirectoryProvider::ScopedTemporaryDirectoryProvider(ScopedTemporaryDirectoryProvider &&rhs)
{
    dir_name_ = rhs.dir_name_;
    rhs.dir_name_.clear();
}

ScopedTemporaryDirectoryProvider & ScopedTemporaryDirectoryProvider::operator=(ScopedTemporaryDirectoryProvider &&rhs)
{
    reset();
    
    dir_name_ = rhs.dir_name_;
    rhs.dir_name_.clear();
    
    return *this;
}

void ScopedTemporaryDirectoryProvider::reset()
{
    if(dir_name_.empty()) {
        return;
    }
    
    wxFileName path = wxFileName::DirName(GetTestTemporaryRootPath());
    path.AppendDir(dir_name_);
    if(path.Rmdir(wxPATH_RMDIR_RECURSIVE) == false) {
        TERRA_DEBUG_LOG(L"failed to remove the temporary directory: " << dir_name_);
    }
    
    dir_name_.clear();
}

String ScopedTemporaryDirectoryProvider::GetPath() const
{
    wxFileName path = wxFileName::DirName(GetTestTemporaryRootPath());
    path.AppendDir(dir_name_);
    
    return path.GetPath();
}

NS_HWM_END
