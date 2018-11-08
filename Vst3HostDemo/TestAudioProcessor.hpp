#pragma once

#include <memory>
#include <utility>
#include <algorithm>
#include <array>
#include <atomic>

#include "./Bypassable.hpp"
#include "./LockFactory.hpp"
#include "./AudioDeviceManager.hpp"
#include "./Vst3Plugin.hpp"
#include "./Transporter.hpp"

NS_HWM_BEGIN

struct Sequence
{
    struct Note {
        SampleCount pos_;
        SampleCount length_;
        int pitch_;
        int velocity_;
        
        SampleCount GetEndPos() const { return pos_ + length_; }
    };
    
    Sequence(std::vector<Note> notes)
    : notes_(std::move(notes))
    {}
    
    std::vector<Note> notes_;
};

class TestAudioProcessor
:   public IAudioDeviceCallback
,   public SingleInstance<TestAudioProcessor>
{
public:
    using PlayingNoteList = std::array<std::atomic<bool>, 128>;
    
    TestAudioProcessor();
    ~TestAudioProcessor();
    
    void SetInstrument(std::shared_ptr<Vst3Plugin> plugin);
    std::shared_ptr<Vst3Plugin> RemoveInstrument();
    
    std::shared_ptr<Vst3Plugin> GetInstrument() const;
    void SetSequence(std::shared_ptr<Sequence> seq);
    
    Transporter & GetTransporter();
    Transporter const & GetTransporter() const;
    
    void StartProcessing(double sample_rate,
                         SampleCount max_block_size,
                         int num_input_channels,
                         int num_output_channels) override;
    
    void Process(SampleCount block_size, float const * const * input, float **output) override;
    
    void StopProcessing() override;
    
    //! 再生中のノート番号が昇順にvectorで返る。
    //! (ex, C3, E3, G3が再生中のときは、{ 48, 52, 55 }が返る。)
    std::vector<int> GetPlayingSequenceNotes() const;
    std::vector<int> GetPlayingInteractiveNotes() const;
    
    void AddInteractiveNote(int note_number);
    void RemoveInteractiveNote(int note_number);
    
private:
    LockFactory lf_;
    std::shared_ptr<Vst3Plugin> plugin_;
    Transporter tp_;
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    BypassFlag bypass_;
    int num_device_inputs_ = 0;
    int num_device_outputs_ = 0;
    std::shared_ptr<Sequence> sequence_;
    PlayingNoteList playing_sequence_notes_;
    PlayingNoteList added_interactive_notes_;
    PlayingNoteList playing_interactive_notes_;
};

NS_HWM_END
