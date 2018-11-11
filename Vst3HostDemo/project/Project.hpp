#pragma once

#include <memory>
#include <utility>
#include <algorithm>
#include <array>
#include <atomic>

#include "../misc/Bypassable.hpp"
#include "../misc/LockFactory.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "../plugin/vst3/Vst3Plugin.hpp"
#include "../transport/Transporter.hpp"

NS_HWM_BEGIN

struct Sequence
{
    struct Note {
        SampleCount pos_ = 0;
        SampleCount length_ = 0;
        UInt8 channel_ = 0;
        UInt8 pitch_ = 0;
        UInt8 velocity_ = 0;
        UInt8 off_velocity_ = 0;
        
        Note() = default;
        Note(SampleCount pos,
             SampleCount length,
             UInt8 channel,
             UInt8 pitch,
             UInt8 velocity,
             UInt8 off_velocity = 0)
        :   pos_(pos)
        ,   length_(length)
        ,   channel_(channel)
        ,   pitch_(pitch)
        ,   velocity_(velocity)
        ,   off_velocity_(off_velocity)
        {}
        
        SampleCount GetEndPos() const { return pos_ + length_; }
    };
    
    Sequence(std::vector<Note> notes)
    : notes_(std::move(notes))
    {}
    
    std::vector<Note> notes_;
};

class Project final
:   public IAudioDeviceCallback
,   public SingleInstance<Project>
{
public:
    struct PlayingNoteInfo
    {
        PlayingNoteInfo(UInt8 channel, UInt8 pitch, UInt8 velocity)
        :   channel_(channel), pitch_(pitch), velocity_(velocity)
        {}
        
        UInt8 channel_ = 0;
        UInt8 pitch_ = 0;
        UInt8 velocity_ = 0;
    };
    
    Project();
    ~Project();
    
    void SetInstrument(std::shared_ptr<Vst3Plugin> plugin);
    std::shared_ptr<Vst3Plugin> RemoveInstrument();
    std::shared_ptr<Vst3Plugin> GetInstrument() const;
    
    std::shared_ptr<Sequence> GetSequence() const;
    void SetSequence(std::shared_ptr<Sequence> seq);
    
    Transporter & GetTransporter();
    Transporter const & GetTransporter() const;
    
    void StartProcessing(double sample_rate,
                         SampleCount max_block_size,
                         int num_input_channels,
                         int num_output_channels) override;
    
    void Process(SampleCount block_size, float const * const * input, float **output) override;
    
    void StopProcessing() override;
    

    
    //! 再生中のシーケンスノート情報のリストが返る。
    std::vector<PlayingNoteInfo> GetPlayingSequenceNotes() const;
    //! 再生中のサンプルノート情報のリストが返る。
    std::vector<PlayingNoteInfo> GetPlayingSampleNotes() const;
    
    void SendSampleNoteOn(UInt8 channel, UInt8 pitch, UInt8 velocity = 64);
    void SendSampleNoteOff(UInt8 channel, UInt8 pitch, UInt8 off_velocity = 0);
    
    double SampleToPPQ(SampleCount sample_pos) const;
    SampleCount PPQToSample(double ppq_pos) const;
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
