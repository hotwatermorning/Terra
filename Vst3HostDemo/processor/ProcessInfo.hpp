#pragma once

#include "../misc/Buffer.hpp"
#include "../misc/ArrayRef.hpp"
#include "../transport/TransportInfo.hpp"

NS_HWM_BEGIN

struct ProcessInfo
{
    struct MidiMessage
    {
        //! フレーム先頭からのオフセット位置
        SampleCount offset_ = 0;
        UInt8 channel_ = 0;
        //! プロジェクト中のPPQ位置
        double ppq_pos_ = 0;
        
        struct NoteOn
        {
            UInt8 pitch_ = 0;
            UInt8 velocity_ = 0;
        };
        
        struct NoteOff
        {
            UInt8 pitch_ = 0;
            UInt8 off_velocity_ = 0;
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
        
        struct PitchBend
        {
            UInt8 value_lsb_ = 0;
            UInt8 value_msb_ = 0;
        };
        
        template<class To>
        To * As() { return mpark::get_if<To>(&data_); }
        
        template<class To>
        To const * As() const { return mpark::get_if<To>(&data_); }
        
        using DataType = std::variant<
            std::monostate,
            NoteOn, NoteOff,
            PolyphonicKeyPressure, ControlChange,
            ProgramChange, ChannelPressure,
            PitchBend
        >;
        DataType data_;
    };
    
    template<class T>
    struct AudioBufferInfo
    {
        BufferRef<T> buffer_;
        SampleCount sample_offset_ = 0;
    };
    
    template<class T>
    struct MidiBufferInfo
    {
        ArrayRef<T> buffer_;
        UInt32 num_used_ = 0;
    };
    
    TransportInfo const *               time_info_ = nullptr;
    AudioBufferInfo<float const>        input_audio_buffer_;
    AudioBufferInfo<float>              output_audio_buffer_;
    MidiBufferInfo<MidiMessage const>   input_midi_buffer_;
    MidiBufferInfo<MidiMessage>         output_midi_buffer_;    
};

NS_HWM_END
