#pragma once

#include "./DeviceIOType.hpp"

NS_HWM_BEGIN

struct MidiDeviceInfo
{
    DeviceIOType io_type_;
    String name_id_;
};

class MidiDevice
{
protected:
    MidiDevice() {}
    
public:
    virtual ~MidiDevice() {}
    
    virtual
    MidiDeviceInfo const & GetDeviceInfo() const = 0;
};

NS_HWM_END
