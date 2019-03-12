#include "Project.hpp"
#include "../transport/Traverser.hpp"
#include "../device/MidiDeviceManager.hpp"
#include "../device/AudioDeviceManager.hpp"
#include "../file/ProjectObjectTable.hpp"
#include "./GraphProcessor.hpp"
#include "../App.hpp"
#include "../misc/MathUtil.hpp"
#include "../misc/StrCnv.hpp"
#include "../misc/Borrowable.hpp"
#include <map>
#include <thread>
#include <atomic>

NS_HWM_BEGIN

namespace {
    
    struct InternalPlayingNoteInfo
    {
        InternalPlayingNoteInfo() noexcept
        {}
        
        InternalPlayingNoteInfo(bool is_note_on, UInt8 velocity) noexcept
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

		PlayingNoteList()
		{}

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
    
    void SetNoteData(ProcessInfo::MidiMessage &msg,
                     bool is_note_on,
                     UInt8 pitch,
                     UInt8 velocity)
    {
        using namespace MidiDataType;
        if(is_note_on) {
            msg.data_ = NoteOn { pitch, velocity };
        } else {
            msg.data_ = NoteOff { pitch, velocity };
        }
    }
    
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
    
    class MidiSequenceDevice : public MidiDevice
    {
    public:
        using BufferRefType = ArrayRef<ProcessInfo::MidiMessage>;
        
        MidiSequenceDevice()
        {
            midi_buffer_.reserve(256);
        }
        
        ~MidiSequenceDevice()
        {}
        
        MidiDeviceInfo const & GetDeviceInfo() const override {
            info_.name_id_ = seq_.name_;
            info_.io_type_ = DeviceIOType::kInput;
            return info_;
        }
        
        Sequence & GetSequence() { return seq_; }
        Sequence const & GetSequence() const { return seq_; }
        void CacheSequence(hwm::IMusicalTimeService *conv)
        {
            cached_sequence_.Set(std::make_shared<CachedSequence>(seq_.MakeCache(conv)));
        }
        
        //! prepare sequence midi messages before calling of GraphProcessor::Process()
        void PrepareEvents(TransportInfo const ti, IMusicalTimeService *ts)
        {            
            auto cache = cached_sequence_.Borrow();
            if(!cache) {
                last_cache_token_ = BorrowableCachedSequence::kInvalidToken;
                return;
            }
            
            bool const is_new_cache = cache.GetToken() != last_cache_token_;
            last_cache_token_ = cache.GetToken();
            
            if(is_new_cache) {
                StopAllNotes();
                
                assert(std::is_sorted(cache->begin(), cache->end(), [](auto const &left, auto const &right) {
                    return left.offset_ < right.offset_;
                }));
                
                cached_iter_ = std::lower_bound(cache->begin(), cache->end(),
                                                ti.play_.begin_.sample_,
                                                [](auto const &left, auto const right) {
                                                    return left.offset_ < right;
                                                });
            }
            
            auto it = cached_iter_;
            
            for( ; it != cache->end(); ++it) {
                auto const &ev = *it;
                assert(ev.offset_ >= ti.play_.begin_.sample_);
                if(ev.offset_ >= ti.play_.end_.sample_) { break; }
                
                ProcessInfo::MidiMessage msg;
                msg.offset_ = ev.offset_ - ti.play_.begin_.sample_;
                msg.channel_ = ev.channel_;
                msg.ppq_pos_ = ts->SampleToTick(ev.offset_) / ts->GetTpqn();
                msg.data_ = ev.data_;

                if(auto p = msg.As<MidiDataType::NoteOn>()) {
                    hwm::dout << "note on [{}][{}]"_format(p->pitch_, ev.offset_) << std::endl;
                    playing_notes_.SetNoteOn(msg.channel_, p->pitch_, p->velocity_);
                } else if(auto p = msg.As<MidiDataType::NoteOff>()) {
                    hwm::dout << "note off [{}][{}]"_format(p->pitch_, ev.offset_) << std::endl;
                    playing_notes_.ClearNote(msg.channel_, p->pitch_);
                }
                
                midi_buffer_.push_back(msg);
            }
            
            cached_iter_ = it;
        }
        
        BufferRefType GetEvents()
        {
            return midi_buffer_;
        }
        
        // 送信済みノートに対するノートオフの送信と、キャッシュトークンのリセットを行う
        void Reset() {
            StopAllNotes();
            last_cache_token_ = BorrowableCachedSequence::kInvalidToken;
        }
        
        void OnAfterFrameProcess()
        {
            midi_buffer_.clear();
        }
        
    private:
        // ノートオフ送信の処理のみを行う。
        void StopAllNotes() {
            playing_notes_.Traverse([&](auto ch, auto pi, auto &x) {
                auto note = x.load();
                if(!note) { return; }

                ProcessInfo::MidiMessage msg;
                msg.offset_ = 0;
                msg.channel_ = ch;
                msg.ppq_pos_ = 0;
                SetNoteData(msg, false, pi, 0);
                midi_buffer_.push_back(msg);

                x.store(InternalPlayingNoteInfo());
            });
        }
        
    private:
        Sequence seq_;
        MidiDeviceInfo mutable info_;
        
        using CachedSequence = std::vector<ProcessInfo::MidiMessage>;
        using BorrowableCachedSequence = Borrowable<CachedSequence>;
        
        BorrowableCachedSequence cached_sequence_;
        BorrowableCachedSequence::TokenType last_cache_token_ = BorrowableCachedSequence::kInvalidToken;
        using cached_iter_t = CachedSequence::const_iterator;
        cached_iter_t cached_iter_;
        std::vector<ProcessInfo::MidiMessage> midi_buffer_;
        PlayingNoteList playing_notes_;
    };
    
    VirtualMidiInDevice kSoftwareKeyboardMidiInput = { L"Software Keyboard" };
}

struct Project::Impl
{
    Impl(Project *pj)
    :   tp_(pj)
    {}
    
    ~Impl()
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
    PlayingNoteList playing_sequence_notes_;
    PlayingNoteList requested_sample_notes_;
    PlayingNoteList playing_sample_notes_;
    SampleCount expected_next_pos_ = 0;
    bool last_playing_ = false;
    std::unique_ptr<GraphProcessor> graph_;
    
    //! input from device
    BufferRef<float const> input_;
    //! output to device
    BufferRef<float> output_;
    
    using MidiSequenceDevicePtr = std::unique_ptr<MidiSequenceDevice>;
    std::vector<MidiSequenceDevicePtr> sequence_devices_;
    using CachedSequence = std::vector<ProcessInfo::MidiMessage>;
    using BorrowableCachedSequence = Borrowable<CachedSequence>;
    
    BorrowableCachedSequence::TokenType last_sequence_cache_token_ = BorrowableCachedSequence::kInvalidToken;
    using cached_iter_t = CachedSequence::const_iterator;
    cached_iter_t cached_iter_;
    Borrowable<CachedSequence> cached_sequence_;
    
    std::vector<DeviceMidiMessage> device_midi_input_buffer_;
    
    class MidiProcessorData
    {
    public:
        MidiProcessorData(MidiDevice *device, Processor *proc)
        :   device_(device)
        ,   proc_(proc)
        {
            buffer_.resize(1024);
        }
        
        MidiDevice *device_;
        Processor *proc_;
        std::vector<ProcessInfo::MidiMessage> buffer_;
    };
    
    class MidiProcessorList
    {
    private:
        std::vector<MidiProcessorData> list_;
        
        template<class Container, class Pred>
        static
        auto FindEntry(Container &cont, Pred pred)
        {
            return std::find_if(cont.begin(), cont.end(), pred);
        }
        
        static
        auto MakePredicate(MidiDevice const *device) {
            return [device](auto const &entry) { return entry.device_ == device; };
        };
        
        static
        auto MakePredicate(Processor const *proc) {
            return [proc](auto const &entry) { return entry.proc_ == proc; };
        };
        
        template<class Container, class Pred>
        static
        auto GetEntryOf(Container &cont, Pred pred)
        {
            auto found = FindEntry(cont, pred);
            auto p = (found == cont.end()) ? nullptr : &*found;
            return p;
        }
        
    public:
        //! device and proc are must be unique in this list.
        void Add(MidiDevice *device, Processor *proc)
        {
            assert(GetProcessorOf(device) == nullptr);
            assert(GetDeviceOf(proc) == nullptr);
            
            list_.push_back(MidiProcessorData(device, proc));
        }
        
        Processor * GetProcessorOf(MidiDevice const *device) const
        {
            if(auto p = GetEntryOf(device)) {
                return p->proc_;
            } else {
                return nullptr;
            }
        }
        
        MidiDevice * GetDeviceOf(Processor const *proc) const
        {
            if(auto p = GetEntryOf(proc)) {
                return p->device_;
            } else {
                return nullptr;
            }
        }
        
        MidiProcessorData * GetEntryOf(MidiDevice const *device)
        {
            return GetEntryOf(list_, MakePredicate(device));
        }
        
        MidiProcessorData const * GetEntryOf(MidiDevice const *device) const
        {
            return GetEntryOf(list_, MakePredicate(device));
        }
        
        MidiProcessorData * GetEntryOf(Processor const *proc)
        {
            return GetEntryOf(list_, MakePredicate(proc));
        }
        
        MidiProcessorData const * GetEntryOf(Processor const *proc) const
        {
            return GetEntryOf(list_, MakePredicate(proc));
        }
        
        void Remove(MidiDevice const *device)
        {
            auto found = FindEntry(list_, [device](auto const &entry) { return entry.device_ == device; });
            if(found != list_.end()) {
                list_.erase(found);
            }
        }
        
        void Remove(Processor const *proc)
        {
            auto found = FindEntry(list_, [proc](auto const &entry) { return entry.proc_ == proc; });
            if(found != list_.end()) {
                list_.erase(found);
            }
        }
        
        auto begin() { return list_.begin(); }
        auto end() { return list_.end(); }
        auto begin() const { return list_.begin(); }
        auto end() const { return list_.end(); }
    };
    
    MidiProcessorList midi_processors_;
};

Project::Project()
:   pimpl_(std::make_unique<Impl>(this))
{
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
    auto proc = GraphProcessor::CreateAudioInput(name, channel_index, num_channels);
    proc->SetCallback([this](GraphProcessor::AudioInput *proc, ProcessInfo const &info) {
        OnSetAudio(proc, info, proc->GetChannelIndex());
    });
    pimpl_->graph_->AddNode(std::move(proc));
}

void Project::AddAudioOutput(String name, UInt32 channel_index, UInt32 num_channels)
{
    auto proc = GraphProcessor::CreateAudioOutput(name, channel_index, num_channels);
    proc->SetCallback([this](GraphProcessor::AudioOutput *proc, ProcessInfo const &info) {
        OnGetAudio(proc, info, proc->GetChannelIndex());
    });
    pimpl_->graph_->AddNode(std::move(proc));
}

void Project::AddMidiInput(MidiDevice *device)
{
    auto proc = GraphProcessor::CreateMidiInput(device->GetDeviceInfo().name_id_);
    proc->SetCallback([this, device](GraphProcessor::MidiInput *proc, ProcessInfo const &info) {
        OnSetMidi(proc, info, device);
    });
    pimpl_->midi_processors_.Add(device, proc.get());
    pimpl_->graph_->AddNode(std::move(proc));
}

void Project::AddMidiOutput(MidiDevice *device)
{
    auto proc = GraphProcessor::CreateMidiOutput(device->GetDeviceInfo().name_id_);
    proc->SetCallback([this, device](GraphProcessor::MidiOutput *proc, ProcessInfo const &info) {
        OnGetMidi(proc, info, device);
    });
    pimpl_->midi_processors_.Add(device, proc.get());
    pimpl_->graph_->AddNode(std::move(proc));
}

void Project::RemoveMidiInput(MidiDevice const *device)
{
    auto proc = pimpl_->midi_processors_.GetProcessorOf(device);
    if(!proc) { return; }
    
    auto node = pimpl_->graph_->GetNodeOf(proc);
    assert(node);
    
    pimpl_->graph_->RemoveNode(node);
    pimpl_->midi_processors_.Remove(proc);
}

void Project::RemoveMidiOutput(MidiDevice const *device)
{
    auto proc = pimpl_->midi_processors_.GetProcessorOf(device);
    if(!proc) { return; }
    
    auto node = pimpl_->graph_->GetNodeOf(proc);
    assert(node);
    
    pimpl_->graph_->RemoveNode(node);
    pimpl_->midi_processors_.Remove(proc);
}

void Project::AddDefaultMidiInputs()
{
    AddMidiInput(&kSoftwareKeyboardMidiInput);
}

UInt32 Project::GetNumSequences() const
{
    return pimpl_->sequence_devices_.size();
}

void Project::AddSequence(String name, UInt32 insert_at)
{
    AddSequence(Sequence(name), insert_at);
}

void Project::AddSequence(Sequence &&seq, UInt32 insert_at)
{
    if(insert_at == -1) { insert_at = GetNumSequences(); }
    auto device = std::make_unique<MidiSequenceDevice>();
    device->GetSequence() = std::move(seq);
    
    auto p = device.get();
    pimpl_->sequence_devices_.insert(pimpl_->sequence_devices_.begin() + insert_at,
                                     std::move(device));
    AddMidiInput(p);
    p->CacheSequence(this);
}

void Project::RemoveSequence(UInt32 index)
{
    assert(index < GetNumSequences());
    
    auto *device = pimpl_->sequence_devices_[index].get();
    RemoveMidiInput(device);
    pimpl_->sequence_devices_.erase(pimpl_->sequence_devices_.begin() + index);
}

Sequence & Project::GetSequence(UInt32 index)
{
    assert(index < GetNumSequences());
    return pimpl_->sequence_devices_[index]->GetSequence();
}

Sequence const & Project::GetSequence(UInt32 index) const
{
    assert(index < GetNumSequences());
    return pimpl_->sequence_devices_[index]->GetSequence();
}

void Project::CacheSequence(UInt32 index)
{
    assert(index < GetNumSequences());
    pimpl_->sequence_devices_[index]->CacheSequence(this);
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
    
    for(int i = 0; i < GetNumSequences(); ++i) {
        auto schema_seq = GetSequence(i).ToSchema();
        auto proc = pimpl_->midi_processors_.GetProcessorOf(pimpl_->sequence_devices_[i].get());
        assert(proc);
        auto node = pimpl_->graph_->GetNodeOf(proc);
        assert(node);
        
        schema_seq->set_node_id(reinterpret_cast<UInt64>(node.get()));
        p->mutable_sequences()->AddAllocated(schema_seq.release());
    }
    
    return p;
}

std::unique_ptr<Project> Project::FromSchema(schema::Project const &schema)
{
    auto p = std::make_unique<Project>();
    
    auto objs = ProjectObjectTable::GetInstance();
    
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
    
    auto mdm = MidiDeviceManager::GetInstance();
    
    std::vector<MidiDevice *> midi_devices_to_add;
    for(auto const &info: mdm->Enumerate()) {
        auto device = mdm->GetDevice(info);
        if(device) { midi_devices_to_add.push_back(device); }
    }
    midi_devices_to_add.push_back(&kSoftwareKeyboardMidiInput);
    
    static auto remove_element = [](auto &container, auto const &elem) {
        container.erase(std::remove(std::begin(container), std::end(container), elem),
                        std::end(container));
    };

    static auto find_element = [](auto &container, auto const &elem) {
        auto found = std::find(std::begin(container), std::end(container), elem);
        auto p = (found != std::end(container)) ? &*found : nullptr;
        return p;
    };
    
    static auto has_element = [](auto &container, auto const &elem) {
        return find_element(container, elem) != nullptr;
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

        std::map<GraphProcessor::Node *, Impl::MidiSequenceDevicePtr> node_to_sequence_device;
        
        std::vector<schema::Sequence> schema_sequences;
        if(schema.sequences().size() > 0) {
            for(auto const &seq: schema.sequences()) { schema_sequences.push_back(seq); }
        } else if(schema.has_deprecated_sequence()) {
            schema_sequences.push_back(schema.deprecated_sequence());
        }
        
        for(auto const &schema_seq: schema_sequences) {
            auto node = objs->nodes_.Find(schema_seq.node_id());
            if(!node) { continue; }
            
            auto sequence = Sequence::FromSchema(schema_seq);
            assert(sequence);
            
            auto device = std::make_unique<MidiSequenceDevice>();
            device->GetSequence() = std::move(*sequence);
            node_to_sequence_device[node] = std::move(device);
        }
        
        std::vector<GraphProcessor::MidiInput *> midi_ins_to_remove;
        std::vector<GraphProcessor::MidiOutput *> midi_outs_to_remove;

        for(int i = 0; i < new_graph->GetNumMidiInputs(); ++i) {
            auto proc = new_graph->GetMidiInput(i);
            auto node = new_graph->GetNodeOf(proc);

            auto sequence_device = std::move(node_to_sequence_device[node.get()]);
            
            if(node->IsConnected() == false && sequence_device == nullptr) {
                midi_ins_to_remove.push_back(proc);
                continue;
            }

            MidiDevice *device = nullptr;
            
            if(sequence_device) {
                device = sequence_device.get();
            } else {
                MidiDeviceInfo info;
                info.io_type_ = DeviceIOType::kInput;
                info.name_id_ = proc->GetName();
                if(auto p = mdm->GetDevice(info)) {
                    device = p;
                } else {
                    if(info == kSoftwareKeyboardMidiInput.GetDeviceInfo()) {
                        device = &kSoftwareKeyboardMidiInput;
                    }
                }
            }
            
            if(device == nullptr) {
                // TODO: add disconnection flag.
                continue;
            }
            
            remove_element(midi_devices_to_add, device);
            
            proc->SetCallback([pj = p.get(), device](GraphProcessor::MidiInput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnSetMidi(proc, pi, device);
                              });
            p->pimpl_->midi_processors_.Add(device, proc);
            if(sequence_device) {
                p->pimpl_->sequence_devices_.push_back(std::move(sequence_device));
            }
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
            
            remove_element(midi_devices_to_add, device);
            
            proc->SetCallback([pj = p.get(), device](GraphProcessor::MidiOutput *proc, ProcessInfo const &pi)
                              {
                                  pj->OnGetMidi(proc, pi, device);
                              });
        }
        
        for(auto proc: midi_ins_to_remove) {
            auto node = new_graph->GetNodeOf(proc);
            if(!node) { continue; }
            
            new_graph->RemoveNode(node);
        }
        for(auto proc: midi_outs_to_remove) {
            auto node = new_graph->GetNodeOf(proc);
            if(!node) { continue; }
            
            new_graph->RemoveNode(node);
        }
    }
    for(auto device: midi_devices_to_add) {
        auto const &info = device->GetDeviceInfo();
        if(info.io_type_ == DeviceIOType::kInput) {
            p->AddMidiInput(device);
        } else {
            // @note midi output devices are not supported yet.
            p->AddMidiOutput(device);
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
    
    auto const num_seqs = GetNumSequences();
    for(UInt32 i = 0; i < num_seqs; ++i) {
        CacheSequence(i);
    }
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

template<class Iter, class Container>
void CheckIterValidity(Iter it, Container const &cont)
{
    auto begin = cont.begin();
    auto end = cont.end();
    auto begin_addr = &cont[0];
    auto end_addr = &cont[0] + cont.size();
    if(begin <= it && it <= end) {
        // do nothing.
    } else {
        assert(false);
    }
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
        for(auto &entry: pimpl_->midi_processors_) {
            entry.buffer_.clear();
        }
        
        if(auto mdm = MidiDeviceManager::GetInstance()) {
            auto const timestamp = mdm->GetMessages(pimpl_->device_midi_input_buffer_);
            auto frame_length = ti.play_.duration_.sec_;
            auto frame_begin_time = timestamp - frame_length;
            for(auto dm: pimpl_->device_midi_input_buffer_) {
                auto entry = pimpl_->midi_processors_.GetEntryOf(dm.device_);
                if(!entry) { continue; }
                
                auto const pos = std::max<double>(0, dm.time_stamp_ - frame_begin_time);
                ProcessInfo::MidiMessage pm((SampleCount)std::round(pos * pimpl_->sample_rate_),
                                            dm.channel_, 0, dm.data_);
                entry->buffer_.push_back(pm);
            }
        }
        
        auto add_note = [&, this](SampleCount sample_abs_pos,
                                  UInt8 channel, UInt8 pitch, UInt8 velocity, bool is_note_on,
                                  MidiDevice *device
                                  )
        {
            ProcessInfo::MidiMessage mm;
            mm.offset_ = sample_abs_pos - ti.play_.begin_.sample_;
            mm.channel_ = channel;
            mm.ppq_pos_ = SampleToTick(sample_abs_pos) / GetTpqn();
            SetNoteData(mm, is_note_on, pitch, velocity);
            
            auto entry = pimpl_->midi_processors_.GetEntryOf(device);
            assert(entry);
            entry->buffer_.push_back(mm);
        };

        bool const need_stop_all_sequence_notes
        = (pimpl_->last_playing_ && (ti.playing_ == false))
        || (ti.play_.begin_.sample_ != pimpl_->expected_next_pos_)
        ;
        
        pimpl_->last_playing_ = ti.playing_;
        pimpl_->expected_next_pos_ = (ti.playing_ ? ti.play_.end_ : ti.play_.begin_).sample_;
        
        //hwm::dout << "#2 " << std::this_thread::get_id() << ": " << cache.get() << ", " << pimpl_->cached_sequence_ << std::endl;
        
        for(auto &seq_dev: pimpl_->sequence_devices_) {
            if(need_stop_all_sequence_notes) {
                seq_dev->Reset();
            }
            
            if(ti.playing_) {
                seq_dev->PrepareEvents(ti, this);
            }
            
            auto entry = pimpl_->midi_processors_.GetEntryOf(seq_dev.get());
            assert(entry);
            
            auto ref = seq_dev->GetEvents();
            entry->buffer_.clear();
            std::copy(ref.begin(), ref.end(), std::back_inserter(entry->buffer_));
            
            seq_dev->OnAfterFrameProcess();
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
    auto entry = pimpl_->midi_processors_.GetEntryOf(input);
    if(!entry) { return; }

    auto const &buffer = entry->buffer_;
#if defined(_DEBUG)
    for(auto const &e: buffer) {
        if(auto p = e.As<MidiDataType::NoteOn>()) {
            hwm::dout << "note on: offset {}, pitch {}"_format(e.offset_, p->pitch_) << std::endl;
        } else if(auto p = e.As<MidiDataType::NoteOff>()) {
            hwm::dout << "note off: offset {}, pitch {}"_format(e.offset_, p->pitch_) << std::endl;
        }
    }
#endif
    input->SetData(buffer);
}

void Project::OnGetMidi(GraphProcessor::MidiOutput *output, ProcessInfo const &pi, MidiDevice *device)
{
}

NS_HWM_END
