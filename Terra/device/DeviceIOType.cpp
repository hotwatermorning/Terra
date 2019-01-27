#include "DeviceIOType.hpp"
#include "../misc/StrCnv.hpp"

NS_HWM_BEGIN

std::string to_string(DeviceIOType io)
{
    return io == DeviceIOType::kInput ? "Input" : "Output";
}

std::wstring to_wstring(DeviceIOType io)
{
    return to_wstr(to_string(io));
}

NS_HWM_END
