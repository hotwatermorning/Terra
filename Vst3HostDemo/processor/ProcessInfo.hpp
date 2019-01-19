#pragma once

#include "../data_type/MidiDataType.hpp"
#include "../misc/Buffer.hpp"
#include "../misc/ArrayRef.hpp"
#include "../transport/TransportInfo.hpp"

NS_HWM_BEGIN

struct ProcessInfo
{
    struct MidiMessage
    {
        using DataType = MidiDataType::VariantType;
        
        //! フレーム先頭からのオフセット位置
        SampleCount offset_ = 0;
        UInt8 channel_ = 0;
        //! プロジェクト中のPPQ位置
        double ppq_pos_ = 0;

        MidiMessage();
        MidiMessage(SampleCount offset, UInt8 channel, double ppq_pos, DataType data);
        
        template<class To>
        To * As() { return mpark::get_if<To>(&data_); }
        
        template<class To>
        To const * As() const { return mpark::get_if<To>(&data_); }
        
        DataType data_;
    };
    
    template<class T>
    struct MidiBufferInfo
    {
        ArrayRef<T> buffer_;
        UInt32 num_used_ = 0;
    };
    
    TransportInfo const *               time_info_ = nullptr;
    BufferRef<float const>              input_audio_buffer_;
    BufferRef<float>                    output_audio_buffer_;
    MidiBufferInfo<MidiMessage const>   input_midi_buffer_;
    MidiBufferInfo<MidiMessage>         output_midi_buffer_;    
};

NS_HWM_END
