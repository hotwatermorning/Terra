#include "ProcessInfo.hpp"

NS_HWM_BEGIN

ProcessInfo::MidiMessage::MidiMessage()
{
    
}

ProcessInfo::MidiMessage::MidiMessage(SampleCount offset, UInt8 channel, double ppq_pos, DataType data)
:   offset_(offset)
,   channel_(channel)
,   ppq_pos_(ppq_pos)
,   data_(data)
{
    assert(mpark::get_if<mpark::monostate>(&data) == nullptr);
}

NS_HWM_END
