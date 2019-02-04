#include "Project.hpp"
#include "../transport/Traverser.hpp"
#include "../device/MidiDeviceManager.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "./GraphProcessor.hpp"
#include "../App.hpp"
#include "../misc/Round.hpp"
#include <map>

NS_HWM_BEGIN

namespace {
    
    class VirtualMidiInDevice : public MidiDevice {
    public:
        VirtualMidiInDevice(String name_id)
        {
            info_.name_id_ = name_id;
            info_.io_type_ = DeviceIOType::kInput;
        }
        
        ~VirtualMidiInDevice() {}
        MidiDeviceInfo const & GetDeviceInfo() const override { return info_; }
    private:
        MidiDeviceInfo info_;
    };
    
    VirtualMidiInDevice kSoftwareKeyboardMidiInput = { L"Software Keyboard" };
    VirtualMidiInDevice kSequencerMidiInput = { L"Sequencer" };
}

struct InternalPlayingNoteInfo
{
    InternalPlayingNoteInfo()
    {}
    
    InternalPlayingNoteInfo(bool is_note_on, UInt8 velocity)
    :   initialized_(true)
    ,   is_note_on_(is_note_on)
    ,   velocity_(velocity)
    {}
    
    bool initialized_ = false;
    bool is_note_on_ = false;
    UInt8 velocity_ = 0; // may be a note off velocity.
    
    bool IsNoteOn() const {
        assert(initialized());
        return is_note_on_;
    }
    
    bool IsNoteOff() const {
        assert(initialized());
        return is_note_on_ == false;
    }
    
    explicit operator bool() const { return initialized_; }
    bool initialized() const { return initialized_; }
};

struct PlayingNoteList
{
    static constexpr UInt8 kNumChannels = 16;
    static constexpr UInt8 kNumPitches = 128;
    
    using value_type = InternalPlayingNoteInfo;
    using ch_container = std::array<std::atomic<InternalPlayingNoteInfo>, kNumPitches>;
    using container = std::array<ch_container, kNumChannels>;
    
    //! @tparam F is a functor where its signature is
    //! `void(UInt8 channel, UInt8 pitch, std::atomic<std::optional<InternalPlayingNoteInfo> &)`
    template<class F>
    void Traverse(F f) { TraverseImpl(list_, f); }
    
    //! @tparam F is a functor where its signature is
    //! `void(UInt8 channel, UInt8 pitch, std::atomic<std::optional<InternalPlayingNoteInfo> &)`
    template<class F>
    void Traverse(F f) const { TraverseImpl(list_, f); }
    
    void Clear()
    {
        Traverse([](auto ch, auto pi, auto &x) { x.store(InternalPlayingNoteInfo()); });
    }
    
    std::vector<Project::PlayingNoteInfo> GetPlayingNotes() const
    {
        std::vector<Project::PlayingNoteInfo> tmp;
        Traverse([&tmp](auto ch, auto pi, auto &x) {
            auto note = x.load();
            if(note && note.IsNoteOn()) {
                tmp.emplace_back(ch, pi, note.velocity_);
            }
        });
        return tmp;
    }
    
    void SetNoteOn(UInt8 channel, UInt8 pitch, UInt8 velocity)
    {
        assert(channel < kNumChannels);
        assert(pitch < kNumPitches);
        list_[channel][pitch] = { true, velocity };
    }
    
    void SetNoteOff(UInt8 channel, UInt8 pitch, UInt8 off_velocity)
    {
        assert(channel < kNumChannels);
        assert(pitch < kNumPitches);
        list_[channel][pitch] = { false, off_velocity };
    }
    
    void ClearNote(UInt8 channel, UInt8 pitch)
    {
        assert(channel < kNumChannels);
        assert(pitch < kNumPitches);
        list_[channel][pitch] = InternalPlayingNoteInfo();
    }
    
    InternalPlayingNoteInfo Get(UInt8 channel, UInt8 pitch) const
    {
        return list_[channel][pitch].load();
    }
    
private:
    template<class List, class F>
    static
    void TraverseImpl(List &list, F f)
    {
        for(UInt8 ch = 0; ch < kNumChannels; ++ch) {
            for(UInt8 pi = 0; pi < kNumPitches; ++pi) {
                f(ch, pi, list[ch][pi]);
            }
        }
    }
    
    container list_;
};

struct Project::Impl
{
    Impl(Project *pj)
    :   tp_(pj)
    {}
    
    LockFactory lf_;
    Transporter tp_;
    bool is_active_ = false;
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    BypassFlag bypass_;
    int num_device_inputs_ = 0;
    int num_device_outputs_ = 0;
    std::shared_ptr<Sequence> sequence_;
    PlayingNoteList playing_sequence_notes_;
    PlayingNoteList requested_sample_notes_;
    PlayingNoteList playing_sample_notes_;
    SampleCount smp_last_pos_ = 0;
    GraphProcessor graph_;
    
    //! input from device
    BufferRef<float const> input_;
    //! output to device
    BufferRef<float> output_;
    
    std::vector<DeviceMidiMessage> device_midi_input_buffer_;
    std::map<MidiDevice const *, std::vector<ProcessInfo::MidiMessage>> midi_input_table_;
};

Project::Project()
:   pimpl_(std::make_unique<Impl>(this))
{
    pimpl_->playing_sequence_notes_.Clear();
    pimpl_->requested_sample_notes_.Clear();
    pimpl_->playing_sample_notes_.Clear();
    pimpl_->device_midi_input_buffer_.reserve(2048);
    AddMidiInput(&kSoftwareKeyboardMidiInput);
    AddMidiInput(&kSequencerMidiInput);
}

Project::~Project()
{}

Project * Project::GetCurrentProject()
{
    auto app = MyApp::GetInstance();
    return app->GetCurrentProject();
}

void Project::AddAudioInput(String name, UInt32 channel_index, UInt32 num_channels)
{
    pimpl_->graph_.AddAudioInput(name, num_channels,
                                 [channel_index, this](GraphProcessor::AudioInput *in,
                                                       ProcessInfo const &pi)
                                 {
                                     OnSetAudio(in, pi, channel_index);
                                 });
}

void Project::AddAudioOutput(String name, UInt32 channel_index, UInt32 num_channels)
{
    pimpl_->graph_.AddAudioOutput(name, num_channels,
                                  [channel_index, this](GraphProcessor::AudioOutput *out,
                                                        ProcessInfo const &pi) {
                                     OnGetAudio(out, pi, channel_index);
                                 });
}

void Project::AddMidiInput(MidiDevice *device)
{
    pimpl_->midi_input_table_[device].reserve(2048);
    pimpl_->graph_.AddMidiInput(device->GetDeviceInfo().name_id_,
                                [device, this](GraphProcessor::MidiInput *in,
                                               ProcessInfo const &pi)
                                {
                                    OnSetMidi(in, pi, device);
                                });
}

void Project::AddMidiOutput(MidiDevice *device)
{
//    pimpl_->graph_.AddMidiOutput(name,
//                                 [device, this](GraphProcessor::MidiOutput *out,
//                                                ProcessInfo const &pi)
//                                 {
//                                     OnGetMidi(out, pi, device);
//                                 });
}

std::shared_ptr<Sequence> Project::GetSequence() const
{
    auto lock = pimpl_->lf_.make_lock();
    return pimpl_->sequence_;
}

void Project::SetSequence(std::shared_ptr<Sequence> seq)
{
    //! フレーム処理中は実行しない。
    //! (seqがフレーム処理の中だけで参照されて、リアルタイムスレッド上でdeleteされてしまうのを防ぐため)
    auto bypass = MakeScopedBypassRequest(pimpl_->bypass_, true);
    
    auto lock = pimpl_->lf_.make_lock();
    pimpl_->sequence_ = seq;
}

Transporter & Project::GetTransporter()
{
    return pimpl_->tp_;
}

Transporter const & Project::GetTransporter() const
{
    return pimpl_->tp_;
}

GraphProcessor & Project::GetGraph()
{
    return pimpl_->graph_;
}

std::vector<Project::PlayingNoteInfo> Project::GetPlayingSequenceNotes() const
{
    return pimpl_->playing_sequence_notes_.GetPlayingNotes();
}

std::vector<Project::PlayingNoteInfo> Project::GetPlayingSampleNotes() const
{
    return pimpl_->playing_sample_notes_.GetPlayingNotes();
}

void Project::SendSampleNoteOn(UInt8 channel, UInt8 pitch, UInt8 velocity)
{
    pimpl_->requested_sample_notes_.SetNoteOn(channel, pitch, velocity);
}

void Project::SendSampleNoteOff(UInt8 channel, UInt8 pitch, UInt8 off_velocity)
{
    pimpl_->requested_sample_notes_.SetNoteOff(channel, pitch, off_velocity);
}

struct ScopedAudioDeviceStopper
{
    ScopedAudioDeviceStopper(AudioDevice *dev)
    :   dev_(dev)
    ,   need_to_restart_(dev->IsStopped() == false)
    {
        dev_->Stop();
    }
    
    ~ScopedAudioDeviceStopper()
    {
        if(need_to_restart_) {
            dev_->Start();
        }
    }
    
    ScopedAudioDeviceStopper(ScopedAudioDeviceStopper const &) = delete;
    ScopedAudioDeviceStopper & operator=(ScopedAudioDeviceStopper const &) = delete;
    ScopedAudioDeviceStopper(ScopedAudioDeviceStopper &&) = delete;
    ScopedAudioDeviceStopper & operator=(ScopedAudioDeviceStopper &&) = delete;
    
private:
    AudioDevice *dev_ = nullptr;
    bool need_to_restart_ = false;
};

void Project::Activate()
{
    //! don't call activate twice.
    assert(IsActive() == false);
    
    auto adm = AudioDeviceManager::GetInstance();
    auto dev = adm->GetDevice();
    if(!dev) { return; }
    
    ScopedAudioDeviceStopper s(dev);
    adm->AddCallback(this);
    
    pimpl_->is_active_ = true;
}

void Project::Deactivate()
{
    if(IsActive() == false) { return; }
    
    auto adm = AudioDeviceManager::GetInstance();
    auto dev = adm->GetDevice();
    if(!dev) { return; }
    
    ScopedAudioDeviceStopper s(dev);
    adm->RemoveCallback(this);
    
    pimpl_->is_active_ = false;
}

bool Project::IsActive() const
{
    return pimpl_->is_active_;
}

double Project::GetSampleRate() const
{
    return pimpl_->sample_rate_;
}

Tick Project::GetTpqn() const
{
    return 480;
}

double Project::TickToSec(double tick) const
{
    return SampleToSec(TickToSample(tick));
}

double Project::SecToTick(double sec) const
{
    return SampleToTick(SecToSample(sec));
}

double Project::TickToSample(double tick) const
{
    // tempo automation is not supported yet.
    auto lock = pimpl_->lf_.make_lock();
    
    auto ppq_pos = tick / GetTpqn();
    return Round<SampleCount>(ppq_pos * 60.0 / GetTempoAt(0) * GetSampleRate());
}

double Project::SampleToTick(double sample) const
{
    // tempo automation is not supported yet.
    auto lock = pimpl_->lf_.make_lock();
    
    double const ppq_pos = (sample / GetSampleRate()) * (GetTempoAt(0) / 60.0);
    return ppq_pos * GetTpqn();
}

double Project::SecToSample(double sec) const
{
    return sec * pimpl_->sample_rate_;
}

double Project::SampleToSec(double sample) const
{
    assert(pimpl_->sample_rate_ > 0);
    return sample / pimpl_->sample_rate_;
}

double Project::TickToPPQ(double tick) const
{
    return tick / GetTpqn();
}

double Project::PPQToTick(double ppq) const
{
    return ppq * GetTpqn();
}

MBT Project::TickToMBT(Tick tick) const
{
    auto tpqn = GetTpqn();
    auto meter = GetMeterAt(tick);
    auto const beat_length = meter.GetBeatLength(tpqn);
    auto const measure_length = meter.GetMeasureLength(tpqn);
    auto const measure_pos = tick / measure_length;
    auto const beat_pos = (tick % measure_length) / beat_length;
    auto const tick_pos = tick % beat_length;
    
    return MBT(measure_pos, beat_pos, tick_pos);
}

Tick Project::MBTToTick(MBT mbt) const
{
    // meter event sequence is not supported yet.
    
    auto const meter = GetMeterAt(0);
    auto const tpqn = GetTpqn();

    return
    mbt.measure_ * meter.GetMeasureLength(tpqn)
    + mbt.beat_ * meter.GetBeatLength(tpqn)
    + mbt.tick_;
}

double Project::GetTempoAt(double tick) const
{
    return 120.0;
}

Meter Project::GetMeterAt(double tick) const
{
    return Meter(4, 4);
}

void Project::StartProcessing(double sample_rate,
                              SampleCount max_block_size,
                              int num_input_channels,
                              int num_output_channels)
{
    pimpl_->sample_rate_ = sample_rate;
    pimpl_->block_size_ = max_block_size;
    pimpl_->num_device_inputs_ = num_input_channels;
    pimpl_->num_device_outputs_ = num_output_channels;
    pimpl_->graph_.StartProcessing(sample_rate, max_block_size);
    
    auto const info = pimpl_->tp_.GetCurrentState();
    SampleCount const sample = Round<SampleCount>(TickToSample(info.play_.begin_.tick_));
    pimpl_->tp_.MoveTo(sample);
    
    SampleCount const loop_begin_sample = Round<SampleCount>(TickToSample(info.loop_.begin_.tick_));
    SampleCount const loop_end_sample = Round<SampleCount>(TickToSample(info.loop_.end_.tick_));
    pimpl_->tp_.SetLoopRange(loop_begin_sample, loop_end_sample);
}

template<class F>
class TraversalCallback
:   public Transporter::Traverser::ITraversalCallback
{
public:
    TraversalCallback(F f) : f_(std::forward<F>(f)) {}
    
    void Process(TransportInfo const &info) override
    {
        f_(info);
    }
    
    F f_;
};

template<class F>
TraversalCallback<F> MakeTraversalCallback(F f)
{
    return TraversalCallback<F>(std::forward<F>(f));
}

void Project::Process(SampleCount block_size, float const * const * input, float **output)
{
    ScopedBypassGuard guard;
    
    for(int i = 0; i < 50; ++i) {
        guard = ScopedBypassGuard(pimpl_->bypass_);
        if(guard) { break; }
    }
    
    if(!guard) { return; }
    
    SampleCount num_processed = 0;
    
    auto cb = MakeTraversalCallback([&, this](TransportInfo const &ti) {
        auto const frame_begin = ti.play_.begin_.sample_;
        auto const frame_end = ti.play_.end_.sample_;
        
        for(auto &entry: pimpl_->midi_input_table_) {
            entry.second.clear();
        }
        
        if(auto mdm = MidiDeviceManager::GetInstance()) {
            auto const timestamp = mdm->GetMessages(pimpl_->device_midi_input_buffer_);
            auto frame_length = ti.play_.duration_.sec_;
            auto frame_begin_time = timestamp - frame_length;
            for(auto dm: pimpl_->device_midi_input_buffer_) {
                auto const pos = std::max<double>(0, dm.time_stamp_ - frame_begin_time);
                ProcessInfo::MidiMessage pm((SampleCount)std::round(pos * pimpl_->sample_rate_),
                                            dm.channel_,
                                            0,
                                            dm.data_);
                pimpl_->midi_input_table_[dm.device_].push_back(pm);
            }
        }
        
        auto in_this_frame = [&](auto pos) { return frame_begin <= pos && pos < frame_end; };
        
        auto add_note = [&, this](SampleCount sample_pos,
                                  UInt8 channel, UInt8 pitch, UInt8 velocity, bool is_note_on,
                                  MidiDevice *device)
        {
            ProcessInfo::MidiMessage mm;
            mm.offset_ = sample_pos - ti.play_.begin_.sample_;
            mm.channel_ = channel;
            mm.ppq_pos_ = SampleToTick(sample_pos) / GetTpqn();
            if(is_note_on) {
                mm.data_ = MidiDataType::NoteOn { pitch, velocity };
            } else {
                mm.data_ = MidiDataType::NoteOff { pitch, velocity };
            }
            pimpl_->midi_input_table_[device].push_back(mm);
        };
        
        std::shared_ptr<Sequence> seq = GetSequence();
        
        bool const need_stop_all_sequence_notes
        = (ti.playing_ == false)
        || (ti.playing_ && ti.play_.begin_.sample_ != pimpl_->smp_last_pos_)
        ;
        
        pimpl_->smp_last_pos_ = ti.play_.end_.sample_;

        if(need_stop_all_sequence_notes) {
            int x = 0;
            x = 1;
            
            pimpl_->playing_sequence_notes_.Traverse([&](auto ch, auto pi, auto &x) {
                auto note = x.load();
                if(note) {
                    assert(note.IsNoteOn());
                    add_note(ti.play_.begin_.sample_, ch, pi, 0, false, &kSequencerMidiInput);
                    x.store(InternalPlayingNoteInfo());
                }
            });
        }
        
        if(ti.playing_) {
            std::for_each(seq->notes_.begin(), seq->notes_.end(), [&](Sequence::Note const &note) {
                if(in_this_frame(note.pos_)) {
                    add_note(note.pos_, note.channel_, note.pitch_, note.velocity_, true, &kSequencerMidiInput);
                    pimpl_->playing_sequence_notes_.SetNoteOn(note.channel_, note.pitch_, note.velocity_);
                }
                if(in_this_frame(note.GetEndPos()-1)) {
                    add_note(note.GetEndPos(), note.channel_, note.pitch_, note.off_velocity_, false, &kSequencerMidiInput);
                    pimpl_->playing_sequence_notes_.ClearNote(note.channel_, note.pitch_);
                }
            });
        }
        
        pimpl_->requested_sample_notes_.Traverse([&](auto ch, auto pi, auto &x) {
            auto note = x.exchange(InternalPlayingNoteInfo());
            auto playing_note = pimpl_->playing_sample_notes_.Get(ch, pi);
            bool const playing = (playing_note && playing_note.IsNoteOn());
            if(!note) { return; }
            if(note.IsNoteOn() && !playing) {
                add_note(ti.play_.begin_.sample_, ch, pi, note.velocity_, true, &kSoftwareKeyboardMidiInput);
                pimpl_->playing_sample_notes_.SetNoteOn(ch, pi, note.velocity_);
            } else if(note.IsNoteOff()) {
                add_note(ti.play_.begin_.sample_, ch, pi, note.velocity_, false, &kSoftwareKeyboardMidiInput);
                pimpl_->playing_sample_notes_.ClearNote(ch, pi);
            }
        });
        
        if(pimpl_->graph_.GetNumAudioInputs() > 0) {
            pimpl_->input_ = BufferRef<float const> {
                input,
                0,
                (UInt32)pimpl_->num_device_inputs_,
                (UInt32)num_processed,
                (UInt32)ti.play_.duration_.sample_
            };
        } else {
            pimpl_->input_ = BufferRef<float const>{};
        }
        
        pimpl_->output_ = BufferRef<float> {
            output,
            0,
            (UInt32)pimpl_->num_device_outputs_,
            (UInt32)num_processed,
            (UInt32)ti.play_.duration_.sample_,
        };
        
        pimpl_->graph_.Process(ti);

        num_processed += ti.play_.duration_.sample_;
    });
    
    Transporter::Traverser tv;
    tv.Traverse(&pimpl_->tp_, block_size, &cb);
}

void Project::StopProcessing()
{
    pimpl_->graph_.StopProcessing();
}

void Project::OnSetAudio(GraphProcessor::AudioInput *input, ProcessInfo const &pi, UInt32 channel_index)
{
    if(pimpl_->input_.samples() == 0) { return; }
    
    auto const num_src_channels = pimpl_->input_.channels();
    auto const num_desired_channels = input->GetAudioChannelCount(BusDirection::kOutputSide);
    
    if(channel_index >= num_src_channels) {
        return;
    }
    
    auto const num_available_channels
    = std::min<int>(num_src_channels, num_desired_channels + channel_index)
    - channel_index;
    
    assert(pi.time_info_->play_.duration_.sample_ == pimpl_->input_.samples());
    BufferRef<float const> ref {
        pimpl_->input_.data(),
        channel_index,
        num_available_channels,
        0,
        pimpl_->input_.samples()
    };
    
    input->SetData(ref);
}

void Project::OnGetAudio(GraphProcessor::AudioOutput *output, ProcessInfo const &pi, UInt32 channel_index)
{
    auto src = output->GetData();
    auto dest = pimpl_->output_;
    
    SampleCount const sample_length = pi.time_info_->play_.duration_.sample_;
    assert(sample_length == src.samples());
    assert(sample_length == dest.samples());
    
    auto const num_desired_channels = output->GetAudioChannelCount(BusDirection::kInputSide);
    auto const num_dest_channels = pimpl_->output_.channels();
    
    if(channel_index >= num_dest_channels) {
        return;
    }
    
    auto const num_available_channels
    = std::min<int>(num_dest_channels, num_desired_channels + channel_index)
    - channel_index;
    
    for(int ch = 0; ch < num_available_channels; ++ch) {
        auto ch_src = src.data()[ch + src.channel_from()] + src.sample_from();
        auto ch_dest = dest.data()[ch + dest.channel_from()] + channel_index + dest.sample_from();
        for(int smp = 0; smp < sample_length; ++smp) {
            ch_dest[smp] += ch_src[smp];
        }
    }
}

void Project::OnSetMidi(GraphProcessor::MidiInput *input, ProcessInfo const &pi, MidiDevice *device)
{
    input->SetData(pimpl_->midi_input_table_[device]);
}

void Project::OnGetMidi(GraphProcessor::MidiOutput *output, ProcessInfo const &pi, MidiDevice *device)
{
}

NS_HWM_END
