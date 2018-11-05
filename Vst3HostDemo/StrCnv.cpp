#include "StrCnv.hpp"
#include <cstdlib>
#include <sstream>
#include <cwchar>
#include <wx/wx.h>

NS_HWM_BEGIN

std::wstring to_wstr(std::string const &str)
{
    return wxString(str.c_str(), wxMBConvUTF8{}).ToStdWstring();
}

std::wstring to_wstr(std::u16string const &str)
{
    return wxString(reinterpret_cast<char const *>(str.c_str()), wxMBConvUTF16LE{}).ToStdWstring();
}

std::string to_utf8(std::wstring const &str)
{
    return wxString(str.c_str()).ToStdString(wxMBConvUTF8{});
}

std::u16string to_utf16(std::wstring const &str)
{
    return reinterpret_cast<char16_t const *>(wxString(str.c_str()).mb_str(wxMBConvUTF16{}).data());
}

NS_HWM_END
