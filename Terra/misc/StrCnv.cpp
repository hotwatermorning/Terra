#include "StrCnv.hpp"
#include <cwchar>
#include <wx/wx.h>

NS_HWM_BEGIN

std::wstring to_wstr(std::wstring const &str) { return str; }

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
    auto ws = wxString(str.c_str());
    auto buf = ws.mb_str(wxMBConvUTF16{});
    auto const it = reinterpret_cast<char16_t const *>(buf.data());
    auto const end = it + buf.length() / 2;
    
    std::u16string u16(it, end);
    return u16;
}

NS_HWM_END
