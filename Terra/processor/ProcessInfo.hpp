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

    struct IEventBuffer
    {
    protected:
        IEventBuffer() {}
        
    public:
        virtual ~IEventBuffer() {}
        
        virtual
        UInt32 GetCount() const = 0;
        
        virtual
        void AddEvent(MidiMessage const &msg) = 0;
        
        virtual
        MidiMessage const & GetEvent(UInt32 index) const = 0;
        
        virtual
        ArrayRef<MidiMessage const> GetRef() const = 0;
    };
    
    struct IEventBufferList
    {
    protected:
        IEventBufferList() {}
        
    public:
        virtual
        ~IEventBufferList() {}
        
        virtual
        UInt32 GetNumBuffers() const = 0;
        
        virtual
        IEventBuffer * GetBuffer(UInt32 index) = 0;
        
        virtual
        IEventBuffer const * GetBuffer(UInt32 index) const = 0;
    };
    
    TransportInfo const *       time_info_ = nullptr;
    BufferRef<float const>      input_audio_buffer_;
    BufferRef<float>            output_audio_buffer_;
    IEventBufferList const *    input_event_buffers_ = nullptr;
    IEventBufferList *          output_event_buffers_ = nullptr;
};

NS_HWM_END
