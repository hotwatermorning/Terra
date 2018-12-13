#pragma once

NS_HWM_BEGIN

namespace MidiDataType
{
    enum MessageType {
        kNoteOff = 0x80,
        kNoteOn = 0x90,
        kPolyphonicKeyPressure = 0xA0,
        kControlChange = 0xB0,
        kProgramChange = 0xC0,
        kChannelPressure = 0xD0,
        kPitchBendChange = 0xE0,
    };
    
    struct NoteOff
    {
        UInt8 pitch_ = 0;
        UInt8 off_velocity_ = 0;
    };

    struct NoteOn
    {
        UInt8 pitch_ = 0;
        UInt8 velocity_ = 0;
    };
    
    struct PolyphonicKeyPressure
    {
        UInt8 pitch_ = 0;
        UInt8 value_ = 0;
    };
    
    struct ControlChange
    {
        UInt8 control_number_ = 0;
        UInt8 data_ = 0;
    };
    
    struct ProgramChange
    {
        UInt8 program_number_ = 0;
    };
    
    struct ChannelPressure
    {
        UInt8 value_ = 0;
    };
    
    struct PitchBendChange
    {
        UInt8 value_lsb_ = 0;
        UInt8 value_msb_ = 0;
    };
    
    using VariantType = std::variant<
        std::monostate,
        NoteOff, NoteOn,
        PolyphonicKeyPressure, ControlChange,
        ProgramChange, ChannelPressure,
        PitchBendChange
    >;
}

NS_HWM_END
