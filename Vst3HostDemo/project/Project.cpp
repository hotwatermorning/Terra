#include "Project.hpp"
#include "../transport/Traverser.hpp"
#include "../device/MidiDeviceManager.hpp"
#include "./GraphProcessor.hpp"
#include <map>

NS_HWM_BEGIN

namespace {
    
    class VirtualMidiDevice : public MidiDevice {
    public:
        VirtualMidiDevice(String name_id) : name_id_(name_id) {}
        ~VirtualMidiDevice() {}
        String GetNameID() const override { return name_id_; }
    private:
        String name_id_;
    };
    
    VirtualMidiDevice kSoftwareKeyboardMidiInput = { L"Software Keyboard" };
    VirtualMidiDevice kSequencerMidiInput = { L"Sequencer" };
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
    UInt8 velocity_ = 0; // may be an note off velocity.
    
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
    LockFactory lf_;
    Transporter tp_;
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    BypassFlag bypass_;
    int num_device_inputs_ = 0;
    int num_device_outputs_ = 0;
    std::shared_ptr<Sequence> sequence_;
    PlayingNoteList playing_sequence_notes_;
    PlayingNoteList requested_sample_notes_;
    PlayingNoteList playing_sample_notes_;
    std::atomic<bool> inputs_enabled_ = { false };
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
:   pimpl_(std::make_unique<Impl>())
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
    pimpl_->graph_.AddMidiInput(device->GetNameID(),
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

Project * Project::GetActiveProject()
{
    return GetInstance();
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

bool Project::CanInputsEnabled() const
{
    return pimpl_->num_device_inputs_ > 0;
}

bool Project::IsInputsEnabled() const
{
    return pimpl_->inputs_enabled_.load();
}

void Project::SetInputsEnabled(bool state)
{
    if(!CanInputsEnabled()) { return; }
    
    pimpl_->inputs_enabled_.store(state);
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

double Project::SampleToPPQ(SampleCount sample_pos) const
{
    // tempo automations is not supported yet.
    auto lock = pimpl_->lf_.make_lock();
    auto info = pimpl_->tp_.GetCurrentState();
    
    return (sample_pos / info.sample_rate_) * (info.tempo_ / 60.0);
}

SampleCount Project::PPQToSample(double ppq_pos) const
{
    // tempo automations is not supported yet.
    auto lock = pimpl_->lf_.make_lock();
    auto info = pimpl_->tp_.GetCurrentState();
    
    return (SampleCount)std::round(ppq_pos * (60.0 / info.tempo_) * info.sample_rate_);
}

//void Project::OnAfterActivated()
//{
//    
//}
//
//void Project::OnBeforeDeactivated()
//{
//    
//}

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
        auto const frame_begin = ti.smp_begin_pos_;
        auto const frame_end = ti.smp_end_pos_;
        
        for(auto &entry: pimpl_->midi_input_table_) {
            entry.second.clear();
        }
        
        if(auto mdm = MidiDeviceManager::GetInstance()) {
            auto const timestamp = mdm->GetMessages(pimpl_->device_midi_input_buffer_);
            auto frame_length = ti.GetSmpDuration() / pimpl_->sample_rate_;
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
            mm.offset_ = sample_pos - ti.smp_begin_pos_;
            mm.channel_ = channel;
            mm.ppq_pos_ = SampleToPPQ(sample_pos);
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
        || (ti.playing_ && ti.smp_begin_pos_ != pimpl_->smp_last_pos_);
        
        pimpl_->smp_last_pos_ = ti.smp_end_pos_;

        if(need_stop_all_sequence_notes) {
            int x = 0;
            x = 1;
            
            pimpl_->playing_sequence_notes_.Traverse([&](auto ch, auto pi, auto &x) {
                auto note = x.load();
                if(note) {
                    assert(note.IsNoteOn());
                    add_note(ti.smp_begin_pos_, ch, pi, 0, false, &kSequencerMidiInput);
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
                add_note(ti.smp_begin_pos_, ch, pi, note.velocity_, true, &kSoftwareKeyboardMidiInput);
                pimpl_->playing_sample_notes_.SetNoteOn(ch, pi, note.velocity_);
            } else if(note.IsNoteOff()) {
                add_note(ti.smp_begin_pos_, ch, pi, note.velocity_, false, &kSoftwareKeyboardMidiInput);
                pimpl_->playing_sample_notes_.ClearNote(ch, pi);
            }
        });
        
        if(pimpl_->inputs_enabled_) {
            pimpl_->input_ = BufferRef<float const> {
                input,
                0,
                (UInt32)pimpl_->num_device_inputs_,
                (UInt32)num_processed,
                (UInt32)ti.GetSmpDuration()
            };
        } else {
            pimpl_->input_ = BufferRef<float const>{};
        }
        
        pimpl_->output_ = BufferRef<float> {
            output,
            0,
            (UInt32)pimpl_->num_device_outputs_,
            (UInt32)num_processed,
            (UInt32)ti.GetSmpDuration(),
        };
        
        pimpl_->graph_.Process(ti);

        num_processed += ti.GetSmpDuration();
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
    
    assert(pi.time_info_->GetSmpDuration() == pimpl_->input_.samples());
    BufferRef<float const> ref {
        pimpl_->input_.data(),
        channel_index,
        input->GetAudioChannelCount(BusDirection::kOutputSide),
        0,
        pimpl_->input_.samples()
    };
    
    input->SetData(ref);
}

void Project::OnGetAudio(GraphProcessor::AudioOutput *output, ProcessInfo const &pi, UInt32 channel_index)
{
    auto src = output->GetData();
    auto dest = pimpl_->output_;
    
    assert(pi.time_info_->GetSmpDuration() == src.samples());
    assert(pi.time_info_->GetSmpDuration() == dest.samples());
    
    for(int ch = 0; ch < src.channels(); ++ch) {
        auto ch_src = src.data()[ch + src.channel_from()] + src.sample_from();
        auto ch_dest = dest.data()[ch + dest.channel_from()] + dest.sample_from();
        for(int smp = 0; smp < src.samples(); ++smp) {
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
