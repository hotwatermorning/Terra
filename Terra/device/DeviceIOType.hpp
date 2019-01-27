#pragma once

NS_HWM_BEGIN

enum class DeviceIOType {
    kInput,
    kOutput,
};

std::string to_string(DeviceIOType io);
std::wstring to_wstring(DeviceIOType io);

NS_HWM_END
