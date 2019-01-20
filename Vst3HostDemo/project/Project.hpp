#pragma once

#include <memory>
#include <utility>
#include <algorithm>
#include <array>
#include <atomic>

#include "../misc/Bypassable.hpp"
#include "../misc/LockFactory.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "../device/MidiDevice.hpp"
#include "../plugin/vst3/Vst3Plugin.hpp"
#include "../transport/Transporter.hpp"
#include "./Sequence.hpp"
#include "./GraphProcessor.hpp"

NS_HWM_BEGIN

class Project final
:   public IAudioDeviceCallback
,   public SingleInstance<Project>
{
public:
    static
    Project * GetActiveProject ();
    
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
    
    void AddAudioInput(String name, UInt32 channel_index, UInt32 num_channel);
    void AddAudioOutput(String name, UInt32 channel_index, UInt32 num_channel);
    void AddMidiInput(MidiDevice *device);
    void AddMidiOutput(MidiDevice *device);
    
//    void SetInstrument(std::shared_ptr<Vst3Plugin> plugin);
//    std::shared_ptr<Vst3Plugin> RemoveInstrument();
//    std::shared_ptr<Vst3Plugin> GetInstrument() const;
    
    std::shared_ptr<Sequence> GetSequence() const;
    void SetSequence(std::shared_ptr<Sequence> seq);
    
    Transporter & GetTransporter();
    Transporter const & GetTransporter() const;
    
    GraphProcessor & GetGraph();
    
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
    
//    void OnAfterActivated();
//    void OnBeforeDeactivated();
    
    void StartProcessing(double sample_rate,
                         SampleCount max_block_size,
                         int num_input_channels,
                         int num_output_channels) override;
    
    void Process(SampleCount block_size, float const * const * input, float **output) override;
    
    void StopProcessing() override;
    
    void OnSetAudio(GraphProcessor::AudioInput *input, ProcessInfo const &pi, UInt32 channel_index);
    void OnGetAudio(GraphProcessor::AudioOutput *output, ProcessInfo const &pi, UInt32 channel_index);
    void OnSetMidi(GraphProcessor::MidiInput *input, ProcessInfo const &pi, MidiDevice *device);
    void OnGetMidi(GraphProcessor::MidiOutput *output, ProcessInfo const &pi, MidiDevice *device);
};

NS_HWM_END
