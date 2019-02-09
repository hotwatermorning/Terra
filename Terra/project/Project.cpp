#include "Project.hpp"
#include "../transport/Traverser.hpp"
#include "../device/MidiDeviceManager.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "./GraphProcessor.hpp"
#include "../App.hpp"
#include "../misc/MathUtil.hpp"
#include "../misc/StrCnv.hpp"
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

template<class T>
struct Borrowable
{
    struct Releaser
    {
        Releaser(Borrowable *owner)
        :   owner_(owner)
        {}
        
        template<class U>
        void operator()(U *p) {
            auto lock = owner_->lf_.make_lock();
            if(owner_->data_ == nullptr) {
                owner_->data_.reset(p);
            } else {
                assert(owner_->released_ == nullptr);
                owner_->released_.reset(p);
            }
        }
        
    private:
        Borrowable *owner_;
    };
    
    //! from non-realtime thread
    void Set(std::unique_ptr<T> x)
    {
        auto lock = lf_.make_lock();
        auto tmp_released = std::move(released_);
        auto tmp_data = std::move(data_);
        data_ = std::move(x);
        lock.unlock();
    }
    
    std::unique_ptr<T, Releaser> Borrow()
    {
        auto lock = lf_.make_lock();
        if(data_) {
            return std::unique_ptr<T, Releaser>(data_.release(), Releaser(this));
        } else {
            return std::unique_ptr<T, Releaser>(released_.release(), Releaser(this));
        }
    }
    
private:
    LockFactory lf_;
    std::unique_ptr<T> data_;
    std::unique_ptr<T> released_;
};

struct Project::Impl
{
    Impl(Project *pj)
    :   tp_(pj)
    {}
    
    String file_name_;
    wxFileName dir_;
    std::unique_ptr<schema::Project> last_schema_;

    LockFactory lf_;
    Transporter tp_;
    bool is_active_ = false;
    double sample_rate_ = 44100;
    SampleCount block_size_ = 256;
    BypassFlag bypass_;
    int num_device_inputs_ = 0;
    int num_device_outputs_ = 0;
    std::unique_ptr<Sequence> sequence_;
    PlayingNoteList playing_sequence_notes_;
    PlayingNoteList requested_sample_notes_;
    PlayingNoteList playing_sample_notes_;
    SampleCount smp_last_pos_ = 0;
    std::unique_ptr<GraphProcessor> graph_;
    
    //! input from device
    BufferRef<float const> input_;
    //! output to device
    BufferRef<float> output_;
    
    using CachedSequence = std::vector<ProcessInfo::MidiMessage>;
    
    CachedSequence *last_cached_sequence_ = nullptr;
    using cached_iter_t = CachedSequence::const_iterator;
    cached_iter_t cached_iter_;
    Borrowable<CachedSequence> cached_sequence_;
    
    std::vector<DeviceMidiMessage> device_midi_input_buffer_;
    std::map<MidiDevice const *, std::vector<ProcessInfo::MidiMessage>> midi_input_table_;
};

Project::Project()
:   pimpl_(std::make_unique<Impl>(this))
{
    pimpl_->sequence_ = std::make_unique<Sequence>();
    pimpl_->graph_ = std::make_unique<GraphProcessor>();
    
    pimpl_->playing_sequence_notes_.Clear();
    pimpl_->requested_sample_notes_.Clear();
    pimpl_->playing_sample_notes_.Clear();
    pimpl_->device_midi_input_buffer_.reserve(2048);
}

Project::~Project()
{}

String const & Project::GetFileName() const
{
    return pimpl_->file_name_;
}

void Project::SetFileName(String const &name)
{
    pimpl_->file_name_ = name;
}

wxFileName const & Project::GetProjectDirectory() const
{
    return pimpl_->dir_;
}

void Project::SetProjectDirectory(wxFileName const &dir_path)
{
    assert(dir_path.HasName() == false);
    pimpl_->dir_ = dir_path;
}

wxFileName Project::GetFullPath() const
{
    if(GetFileName().empty() || GetProjectDirectory().GetFullPath().empty()) {
        return wxFileName();
    } else {
        auto path = GetProjectDirectory();
        path.SetFullName(GetFileName());
        return path;
    }
}

Project * Project::GetCurrentProject()
{
    auto app = MyApp::GetInstance();
    return app->GetCurrentProject();
}

void Project::AddAudioInput(String name, UInt32 channel_index, UInt32 num_channels)
{
    pimpl_->graph_
    ->AddAudioInput(name, channel_index, num_channels)
    ->SetCallback([this](GraphProcessor::AudioInput *in, ProcessInfo const &pi)
                  {
                      OnSetAudio(in, pi, in->GetChannelIndex());
                  });
}

void Project::AddAudioOutput(String name, UInt32 channel_index, UInt32 num_channels)
{
    pimpl_->graph_
    ->AddAudioOutput(name, channel_index, num_channels)
    ->SetCallback([this](GraphProcessor::AudioOutput *out, ProcessInfo const &pi)
                  {
                      OnGetAudio(out, pi, out->GetChannelIndex());
                  });
}

void Project::AddMidiInput(MidiDevice *device)
{
    pimpl_->midi_input_table_[device].reserve(2048);
    pimpl_->graph_
    ->AddMidiInput(device->GetDeviceInfo().name_id_)
    ->SetCallback([device, this](GraphProcessor::MidiInput *in, ProcessInfo const &pi)
                  {
                      OnSetMidi(in, pi, device);
                  });
}

void Project::AddMidiOutput(MidiDevice *device)
{
//    pimpl_->graph_
//    ->AddMidiOutput(device->GetDeviceInfo().name_id_)
//    ->SetCallback([device, this](GraphProcessor::MidiOutput *in, ProcessInfo const &pi)
//                  {
//                      OnGetMidi(in, pi, device);
//                  });
}

void Project::AddDefaultMidiInputs()
{
    AddMidiInput(&kSoftwareKeyboardMidiInput);
    AddMidiInput(&kSequencerMidiInput);
}

Sequence & Project::GetSequence() const
{
    return *pimpl_->sequence_;
}

void Project::CacheSequence()
{
    auto cache = pimpl_->sequence_->Cache(this);
    pimpl_->cached_sequence_.Set(std::make_unique<decltype(cache)>(std::move(cache)));
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
    return *pimpl_->graph_;
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

std::unique_ptr<schema::Project> Project::ToSchema() const
{
    auto p = std::make_unique<schema::Project>();
    
    p->set_name(to_utf8(GetFileName()));
    p->set_block_size(pimpl_->block_size_);
    p->set_sample_rate(pimpl_->sample_rate_);
    
    auto mps = p->mutable_musical_parameters();
    
    auto tempo = mps->add_tempo_events();
    tempo->set_pos(0);
    tempo->set_value(120.0);
    
    auto meter = mps->add_meter_events();
    meter->set_pos(0);
    meter->set_numer(4);
    meter->set_denom(4);

    auto tp_info = pimpl_->tp_.GetCurrentState();
    auto tp = p->mutable_transport();
    if(tp_info.playing_) {
        tp->set_pos(pimpl_->tp_.GetLastMovedPos().sample_);
    } else {
        tp->set_pos(tp_info.play_.begin_.sample_);
    }
    tp->set_loop_begin(tp_info.loop_.begin_.sample_);
    tp->set_loop_end(tp_info.loop_.end_.sample_);
    tp->set_loop_enabled(tp_info.loop_enabled_);
    
    auto graph = pimpl_->graph_->ToSchema();
    p->set_allocated_graph(graph.release());
    
    auto seq = pimpl_->sequence_->ToSchema();
    p->set_allocated_sequence(seq.release());
    
    return p;
}

std::unique_ptr<Project> Project::FromSchema(schema::Project const &schema)
{
    auto p = std::make_unique<Project>();
    
    p->pimpl_->sample_rate_ = std::max<double>(schema.sample_rate(), 22050.0);
    p->pimpl_->block_size_ = std::max<UInt32>(schema.block_size(), 16);
    
    if(schema.has_musical_parameters()) {
        auto &mp = schema.musical_parameters();
        // not supported yet.
    }
    
    if(schema.has_transport()) {
        auto &tp = schema.transport();
        p->pimpl_->tp_.MoveTo(tp.pos());
        p->pimpl_->tp_.SetLoopRange(tp.loop_begin(), tp.loop_end());
        p->pimpl_->tp_.SetLoopEnabled(tp.loop_enabled());
    }
    
    if(schema.has_sequence()) {
        auto new_sequence = Sequence::FromSchema(schema.sequence());
        assert(new_sequence);
        p->pimpl_->sequence_ = std::move(new_sequence);
    }
    
    auto mdm = MidiDeviceManager::GetInstance();
    
    std::vector<MidiDevice *> opened_midi_devices;
    for(auto const &info: mdm->Enumerate()) {
        auto device = mdm->GetDevice(info);
        if(device) { opened_midi_devices.push_back(device); }
    }
    opened_midi_devices.push_back(&kSequencerMidiInput);
    opened_midi_devices.push_back(&kSoftwareKeyboardMidiInput);
    
    auto remove_element = [&](auto &container, auto const &elem) {
        container.erase(std::find(std::begin(container),
                                  std::end(container),
                                  elem),
                        std::end(container));
    };
    
    if(schema.has_graph()) {
        p->pimpl_->graph_ = GraphProcessor::FromSchema(schema.graph());
        auto &new_graph = p->pimpl_->graph_;
        assert(new_graph);
        
        for(int i = 0; i < new_graph->GetNumAudioInputs(); ++i) {
            auto proc = new_graph->GetAudioInput(i);
            proc->SetCallback([pj = p.get()](GraphProcessor::AudioInput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnSetAudio(proc, pi, proc->GetChannelIndex());
                              });
        }
        
        for(int i = 0; i < new_graph->GetNumAudioOutputs(); ++i) {
            auto proc = new_graph->GetAudioOutput(i);
            proc->SetCallback([pj = p.get()](GraphProcessor::AudioOutput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnGetAudio(proc, pi, proc->GetChannelIndex());
                              });
        }
        
        std::vector<GraphProcessor::MidiInput *> midi_ins_to_remove;
        std::vector<GraphProcessor::MidiOutput *> midi_outs_to_remove;

        for(int i = 0; i < new_graph->GetNumMidiInputs(); ++i) {
            auto proc = new_graph->GetMidiInput(i);
            auto node = new_graph->GetNodeOf(proc);
            if(node->GetMidiConnections(BusDirection::kOutputSide).empty()) {
                midi_ins_to_remove.push_back(proc);
                continue;
            }
            
            MidiDeviceInfo info;
            info.io_type_ = DeviceIOType::kInput;
            info.name_id_ = proc->GetName();
            auto device = mdm->GetDevice(info);
            if(device == nullptr) {
                if(info == kSoftwareKeyboardMidiInput.GetDeviceInfo()) {
                    device = &kSoftwareKeyboardMidiInput;
                } else if(info == kSequencerMidiInput.GetDeviceInfo()) {
                    device = &kSequencerMidiInput;
                }
            }
            
            if(device == nullptr) {
                // TODO: add disconnection flag.
                continue;
            }
            
            remove_element(opened_midi_devices, device);
            
            p->pimpl_->midi_input_table_[device].reserve(2048);
            proc->SetCallback([pj = p.get(), device](GraphProcessor::MidiInput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnSetMidi(proc, pi, device);
                              });
        }
        
        for(int i = 0; i < new_graph->GetNumMidiOutputs(); ++i) {
            auto proc = new_graph->GetMidiOutput(i);
            auto node = new_graph->GetNodeOf(proc);
            if(node->GetMidiConnections(BusDirection::kInputSide).empty()) {
                midi_outs_to_remove.push_back(proc);
                continue;
            }
            
            MidiDeviceInfo info;
            info.io_type_ = DeviceIOType::kOutput;
            info.name_id_ = proc->GetName();
            auto device = mdm->GetDevice(info);
            if(device == nullptr) {
                // TODO: add disconnection flag.
                continue;
            }
            
            remove_element(opened_midi_devices, device);
            
            proc->SetCallback([pj = p.get(), device](GraphProcessor::MidiOutput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnGetMidi(proc, pi, device);
                              });
        }
        
        for(auto proc: midi_ins_to_remove) {
            new_graph->RemoveNode(proc);
        }
        for(auto proc: midi_outs_to_remove) {
            new_graph->RemoveNode(proc);
        }
    }
    for(auto device: opened_midi_devices) {
        auto const &info = device->GetDeviceInfo();
        if(info.io_type_ == DeviceIOType::kInput) {
            p->pimpl_->midi_input_table_[device].reserve(2048);
            p->pimpl_->graph_
            ->AddMidiInput(info.name_id_)
            ->SetCallback([pj = p.get(), device](GraphProcessor::MidiInput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnSetMidi(proc, pi, device);
                              });
        } else {
            // @note midi output devices are not supported yet.
//            p->pimpl_->graph_
//            ->AddMidiOutput(info.name_id_)
//            ->SetCallback([pj = p.get(), device](GraphProcessor::MidiOutput *proc, ProcessInfo const &pi)
//                          {
//                              pj->OnGetMidi(proc, pi, device);
//                          });
        }
    }
        
    return p;
}

schema::Project * Project::GetLastSchema() const
{
    return pimpl_->last_schema_.get();
}

void Project::UpdateLastSchema(std::unique_ptr<schema::Project> schema)
{
    pimpl_->last_schema_ = std::move(schema);
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
    pimpl_->graph_->StartProcessing(sample_rate, max_block_size);
    
    auto const info = pimpl_->tp_.GetCurrentState();
    SampleCount const sample = Round<SampleCount>(TickToSample(info.play_.begin_.tick_));
    pimpl_->tp_.MoveTo(sample);
    
    SampleCount const loop_begin_sample = Round<SampleCount>(TickToSample(info.loop_.begin_.tick_));
    SampleCount const loop_end_sample = Round<SampleCount>(TickToSample(info.loop_.end_.tick_));
    pimpl_->tp_.SetLoopRange(loop_begin_sample, loop_end_sample);
    
    CacheSequence();
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
        for(auto &entry: pimpl_->midi_input_table_) {
            entry.second.clear();
        }
        
        if(auto mdm = MidiDeviceManager::GetInstance()) {
            auto const timestamp = mdm->GetMessages(pimpl_->device_midi_input_buffer_);
            auto frame_length = ti.play_.duration_.sec_;
            auto frame_begin_time = timestamp - frame_length;
            for(auto dm: pimpl_->device_midi_input_buffer_) {
                assert(pimpl_->midi_input_table_.count(dm.device_) == 1);
                
                auto const pos = std::max<double>(0, dm.time_stamp_ - frame_begin_time);
                ProcessInfo::MidiMessage pm((SampleCount)std::round(pos * pimpl_->sample_rate_),
                                            dm.channel_, 0, dm.data_);
                pimpl_->midi_input_table_[dm.device_].push_back(pm);
            }
        }
        
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
            
            assert(pimpl_->midi_input_table_.count(device) == 1);
            pimpl_->midi_input_table_[device].push_back(mm);
        };
        
        auto cache = pimpl_->cached_sequence_.Borrow();
        assert(cache != nullptr);
        
        bool const need_stop_all_sequence_notes
        = (ti.playing_ == false)
        || (ti.playing_ && ti.play_.begin_.sample_ != pimpl_->smp_last_pos_)
        || (cache.get() != pimpl_->last_cached_sequence_)
        ;
        
        pimpl_->last_cached_sequence_ = cache.get();
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
            
            pimpl_->cached_iter_ = std::lower_bound(cache->begin(), cache->end(),
                                                    ti.play_.begin_.sample_,
                                                    [](auto const &left, auto const right) {
                                                        return left.offset_ < right;
                                                    });
        }
        
        if(ti.playing_) {
            for(auto &it = pimpl_->cached_iter_; it != cache->end(); ++it) {
                auto const &ev = *it;
                if(ev.offset_ >= ti.play_.end_.sample_) { break; }
                
                if(auto p = ev.As<MidiDataType::NoteOn>()) {
                    add_note(ev.offset_, ev.channel_, p->pitch_, p->velocity_, true, &kSequencerMidiInput);
                    pimpl_->playing_sequence_notes_.SetNoteOn(ev.channel_, p->pitch_, p->velocity_);
                } else if(auto p = ev.As<MidiDataType::NoteOff>()) {
                    add_note(ev.offset_, ev.channel_, p->pitch_, p->off_velocity_, false, &kSequencerMidiInput);
                    pimpl_->playing_sequence_notes_.ClearNote(ev.channel_, p->pitch_);
                }
            }
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
        
        if(pimpl_->graph_->GetNumAudioInputs() > 0) {
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
        
        pimpl_->graph_->Process(ti);

        num_processed += ti.play_.duration_.sample_;
    });
    
    Transporter::Traverser tv;
    tv.Traverse(&pimpl_->tp_, block_size, &cb);
}

void Project::StopProcessing()
{
    pimpl_->graph_->StopProcessing();
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
