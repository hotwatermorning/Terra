#include "Project.hpp"
#include "../transport/Traverser.hpp"
#include "../device/MidiDeviceManager.hpp"

NS_HWM_BEGIN

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
    std::shared_ptr<Vst3Plugin> plugin_;
    std::vector<ProcessInfo::MidiMessage> midis_;
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
    std::vector<DeviceMidiMessage> device_midis_;
};

Project::Project()
:   pimpl_(std::make_unique<Impl>())
{
    pimpl_->playing_sequence_notes_.Clear();
    pimpl_->requested_sample_notes_.Clear();
    pimpl_->playing_sample_notes_.Clear();
    pimpl_->midis_.reserve(128);
    pimpl_->device_midis_.reserve(2048);
}

Project::~Project()
{}

Project * Project::GetActiveProject()
{
    return GetInstance();
}

void Project::SetInstrument(std::shared_ptr<Vst3Plugin> plugin)
{
    assert(plugin);
    
    RemoveInstrument();

    //! sample_rate_とblock_size_が変更される処理は、再生スレッドの処理の前に完了している。
    plugin->SetBlockSize(pimpl_->block_size_);
    plugin->SetSamplingRate(pimpl_->sample_rate_);
    plugin->Resume();
    
    //! フレーム処理中は実行しない。
    //! (既存のpluginがフレーム処理の中だけで参照されて、リアルタイムスレッド上でdeleteされてしまうのを防ぐため)
    auto bypass = MakeScopedBypassRequest(pimpl_->bypass_, true);
    
    auto lock = pimpl_->lf_.make_lock();
    pimpl_->plugin_ = plugin;
}

std::shared_ptr<Vst3Plugin> Project::RemoveInstrument()
{
    //! フレーム処理中は実行しない。
    //! (seqがフレーム処理の中だけで参照されて、リアルタイムスレッド上でdeleteされてしまうのを防ぐため)
    auto bypass = MakeScopedBypassRequest(pimpl_->bypass_, true);
    
    auto lock = pimpl_->lf_.make_lock();
    auto plugin = std::move(pimpl_->plugin_);
    lock.unlock();

    bypass.reset();

    pimpl_->playing_sequence_notes_.Clear();
    pimpl_->requested_sample_notes_.Clear();
    pimpl_->playing_sample_notes_.Clear();
    if(plugin) {
        plugin->Suspend();
    }
    
    return plugin;
}

std::shared_ptr<Vst3Plugin> Project::GetInstrument() const
{
    auto lock = pimpl_->lf_.make_lock();
    return pimpl_->plugin_;
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

void Project::StartProcessing(double sample_rate,
                              SampleCount max_block_size,
                              int num_input_channels,
                              int num_output_channels)
{
    pimpl_->sample_rate_ = sample_rate;
    pimpl_->block_size_ = max_block_size;
    pimpl_->num_device_inputs_ = num_input_channels;
    pimpl_->num_device_outputs_ = num_output_channels;
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
    
    auto plugin = GetInstrument();
    if(!plugin) { return; }
    
    SampleCount num_processed = 0;
    
    auto cb = MakeTraversalCallback([&, this](TransportInfo const &ti) {
        auto const frame_begin = ti.smp_begin_pos_;
        auto const frame_end = ti.smp_end_pos_;
        
        pimpl_->midis_.clear();
        {
            auto mdm = MidiDeviceManager::GetInstance();
            auto const timestamp = mdm->GetMessages(pimpl_->device_midis_);
            
            auto frame_length = ti.GetSmpDuration() / pimpl_->sample_rate_;
            auto frame_begin_time = timestamp - frame_length;
            pimpl_->midis_.clear();
            for(auto dm: pimpl_->device_midis_) {
                auto const pos = std::max<double>(0, dm.time_stamp_ - frame_begin_time);
                ProcessInfo::MidiMessage pm((SampleCount)std::round(pos * pimpl_->sample_rate_),
                                            dm.channel_,
                                            0,
                                            dm.data_);
                pimpl_->midis_.push_back(pm);
            }
        }
        
        auto in_this_frame = [&](auto pos) { return frame_begin <= pos && pos < frame_end; };
        
        auto add_note = [&, this](SampleCount sample_pos, UInt8 channel, UInt8 pitch, UInt8 velocity, bool is_note_on)
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
            pimpl_->midis_.push_back(mm);
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
                    add_note(ti.smp_begin_pos_, ch, pi, 0, false);
                    x.store(InternalPlayingNoteInfo());
                }
            });
        }
        
        if(ti.playing_) {
            std::for_each(seq->notes_.begin(), seq->notes_.end(), [&](Sequence::Note const &note) {
                if(in_this_frame(note.pos_)) {
                    add_note(note.pos_, note.channel_, note.pitch_, note.velocity_, true);
                    pimpl_->playing_sequence_notes_.SetNoteOn(note.channel_, note.pitch_, note.velocity_);
                }
                if(in_this_frame(note.GetEndPos()-1)) {
                    add_note(note.GetEndPos(), note.channel_, note.pitch_, note.off_velocity_, false);
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
                add_note(ti.smp_begin_pos_, ch, pi, note.velocity_, true);
                pimpl_->playing_sample_notes_.SetNoteOn(ch, pi, note.velocity_);
            } else if(note.IsNoteOff()) {
                add_note(ti.smp_begin_pos_, ch, pi, note.velocity_, false);
                pimpl_->playing_sample_notes_.ClearNote(ch, pi);
            }
        });
        
        ProcessInfo pi;
        pi.time_info_ = &ti;
        if(pimpl_->inputs_enabled_) {
            pi.input_audio_buffer_ = { BufferRef<float const>(input, pimpl_->num_device_inputs_, block_size), num_processed };
        }
        pi.output_audio_buffer_ = { BufferRef<float>(output, pimpl_->num_device_outputs_, block_size), num_processed };
        pi.input_midi_buffer_ = { pimpl_->midis_, (UInt32)pimpl_->midis_.size() };

        plugin->Process(pi);

        num_processed += ti.GetSmpDuration();
    });
    
    Transporter::Traverser tv;
    tv.Traverse(&pimpl_->tp_, block_size, &cb);
}

void Project::StopProcessing()
{
    
}

NS_HWM_END
