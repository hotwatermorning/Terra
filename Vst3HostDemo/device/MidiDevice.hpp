#pragma once

NS_HWM_BEGIN

class MidiDevice {
public:
    virtual ~MidiDevice() {}
    virtual String GetNameID() const = 0;
};

NS_HWM_END
