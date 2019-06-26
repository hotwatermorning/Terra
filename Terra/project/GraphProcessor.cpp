#include "./GraphProcessor.hpp"
#include "../processor/EventBuffer.hpp"
#include "../misc/StrCnv.hpp"
#include "../file/ProjectObjectTable.hpp"

NS_HWM_BEGIN

class AudioInputImpl : public GraphProcessor::AudioInput
{
public:
    AudioInputImpl(String name, UInt32 channel_index, UInt32 num_channels)
    :   name_(name)
    ,   num_channels_(num_channels)
    ,   channel_index_(channel_index)
    {
    }

    //! The callback must be set before `OnStartProcessing()`
    void SetCallback(std::function<void(AudioInput *, ProcessInfo const &)> callback) override
    {
        assert(callback != nullptr);
        callback_ = callback;
    }
    
    void SetData(BufferRef<float const> buf) override
    {
        ref_ = buf;
    }
    
    String GetName() const override { return name_; }
    
    UInt32 GetAudioChannelCount(BusDirection dir) const override
    {
        return (dir == BusDirection::kOutputSide) ? num_channels_ : 0;
    }
    
    UInt32 GetChannelIndex() const override { return channel_index_; }
    
    void doProcess(ProcessInfo &pi) override
    {
        assert(callback_);
        
        callback_(this, pi);
        auto dest = pi.output_audio_buffer_;
        
        auto channels = std::min<int>(dest.channels(), ref_.channels());
        for(int ch = 0; ch < channels; ++ch) {
            auto ch_src = ref_.get_channel_data(ch);
            auto ch_dest = dest.get_channel_data(ch);
            std::copy_n(ch_src, pi.time_info_->play_.duration_.sample_, ch_dest);
        }
    }
    
    std::unique_ptr<schema::Processor> ToSchemaImpl() const override
    {
        auto schema = std::make_unique<schema::Processor>();
        auto data = schema->mutable_audio_input_data();
        data->set_name(to_utf8(name_));
        data->set_num_channels(num_channels_);
        data->set_channel_index(channel_index_);
        
        return schema;
    }
    
    static
    std::unique_ptr<AudioInput> FromSchemaImpl(schema::Processor const &schema)
    {
        assert(schema.has_audio_input_data());
        
        auto const &data = schema.audio_input_data();
        return std::make_unique<AudioInputImpl>(to_wstr(data.name()),
                                                data.channel_index(),
                                                data.num_channels());
    }
    
private:
    String name_;
    UInt32 num_channels_;
    UInt32 channel_index_;
    std::function<void(AudioInput *, ProcessInfo const &)> callback_;
    BufferRef<float const> ref_;
};

class AudioOutputImpl : public GraphProcessor::AudioOutput
{
public:
    AudioOutputImpl(String name, UInt32 channel_index, UInt32 num_channels)
    :   name_(name)
    ,   num_channels_(num_channels)
    ,   channel_index_(channel_index)
    {
        SetVolumeLevelImmediately(-10.0);
    }
    
    //! This must be called before `OnStartProcessing()`
    void SetCallback(std::function<void(AudioOutput *, ProcessInfo const &)> callback) override
    {
        assert(callback != nullptr);
        callback_ = callback;
    }
    
    BufferRef<float const> GetData() const override
    {
        return ref_;
    }
    
    String GetName() const override { return name_; }
    
    UInt32 GetAudioChannelCount(BusDirection dir) const override
    {
        return (dir == BusDirection::kInputSide) ? num_channels_ : 0;
    }
    
    UInt32 GetChannelIndex() const override { return channel_index_; }
    
    bool IsGainFaderEnabled() const { return true; }
    
    void doOnStartProcessing(double sample_rate, SampleCount block_size) override
    {
        output_.resize(num_channels_, block_size);
    }
    
    void doProcess(ProcessInfo &pi) override
    {
        assert(callback_);
        
        auto const input = pi.input_audio_buffer_;
        auto const channels_to_copy = std::min(output_.channels(),
                                               input.channels() - input.channel_from()
                                               );
        assert(channels_to_copy == input.channels() - input.channel_from());
        
        auto const samples_to_copy = std::min(output_.samples(),
                                              input.samples() - input.sample_from()
                                              );
        assert(samples_to_copy == input.samples() - input.sample_from());
        assert(samples_to_copy == pi.time_info_->play_.duration_.sample_);
        
        for(int ch = 0; ch < channels_to_copy; ++ch) {
            auto const * const src = input.data()[ch + input.channel_from()] + input.sample_from();
            auto * const dest = output_.data()[ch];
            
            std::copy_n(src, samples_to_copy, dest);
        }
        
        pi.output_audio_buffer_ = BufferRef<float>{ output_, 0, channels_to_copy, 0, samples_to_copy };

        pi_ = pi;
        ref_ = BufferRef<float const>{ output_, 0, channels_to_copy, 0, samples_to_copy };
    }
    
    void ProcessPostFader()
    {
        callback_(this, pi_);
    }
    
    std::unique_ptr<schema::Processor> ToSchemaImpl() const override
    {
        auto schema = std::make_unique<schema::Processor>();
        auto data = schema->mutable_audio_output_data();
        data->set_name(to_utf8(name_));
        data->set_num_channels(num_channels_);
        data->set_channel_index(channel_index_);
        
        return schema;
    }
    
    static
    std::unique_ptr<AudioOutput> FromSchemaImpl(schema::Processor const &schema)
    {
        assert(schema.has_audio_output_data());
        
        auto const &data = schema.audio_output_data();
        return std::make_unique<AudioOutputImpl>(to_wstr(data.name()),
                                                 data.channel_index(),
                                                 data.num_channels());
    }
    
private:
    String name_;
    UInt32 num_channels_;
    UInt32 channel_index_;
    std::function<void(AudioOutput *, ProcessInfo const &)> callback_;
    BufferRef<float const> ref_;
    Buffer<float> output_;
    ProcessInfo pi_;
};

class MidiInputImpl : public GraphProcessor::MidiInput
{
public:
    MidiInputImpl(String name)
    :   name_(name)
    {
    }
    
    //! This must be called before `OnStartProcessing()`
    void SetCallback(std::function<void(MidiInput *, ProcessInfo const &)> callback) override
    {
        assert(callback != nullptr);
        callback_ = callback;
    }
    
    void SetData(BufferType buf) override
    {
        ref_ = buf;
    }
    
    String GetName() const override { return name_; }
    
    UInt32 GetMidiChannelCount(BusDirection dir) const override
    {
        return (dir == BusDirection::kOutputSide) ? 1 : 0;
    }
    
    void doProcess(ProcessInfo &pi) override
    {
        assert(callback_);
        
        callback_(this, pi);
        
        auto &dest = pi.output_event_buffers_;
        auto dest_buf = dest->GetBuffer(0);
        
        for(int i = 0; i < ref_.size(); ++i) {
            dest_buf->AddEvent(ref_[i]);
        }
        
        ref_ = BufferType();
    }

    std::unique_ptr<schema::Processor> ToSchemaImpl() const override
    {
        auto schema = std::make_unique<schema::Processor>();
        auto data = schema->mutable_midi_input_data();
        data->set_name(to_utf8(name_));
        
        return schema;
    }
    
    static
    std::unique_ptr<MidiInput> FromSchemaImpl(schema::Processor const &schema)
    {
        
        assert(schema.has_midi_input_data());
        
        auto const &data = schema.midi_input_data();
        return std::make_unique<MidiInputImpl>(to_wstr(data.name()));
    }
    
private:
    String name_;
    std::function<void(MidiInput *, ProcessInfo const &)> callback_;
    BufferType ref_;
};

class MidiOutputImpl : public GraphProcessor::MidiOutput
{
public:
    MidiOutputImpl(String name)
    :   name_(name)
    {
    }
    
    //! This must be called before `OnStartProcessing()`
    void SetCallback(std::function<void(MidiOutput *, ProcessInfo const &)> callback) override
    {
        assert(callback != nullptr);
        callback_ = callback;
    }
    
    BufferType GetData() const override
    {
        return ref_;
    }
    
    String GetName() const override { return name_; }
    
    UInt32 GetMidiChannelCount(BusDirection dir) const override
    {
        return (dir == BusDirection::kInputSide) ? 1 : 0;
    }
    
    void doProcess(ProcessInfo &pi) override
    {
        assert(callback_);
        
        auto &src = pi.input_event_buffers_;
        auto src_buf = src->GetBuffer(0);
        
        ref_ = src_buf->GetRef();
        callback_(this, pi);
        ref_ = BufferType();
    }
    
    std::unique_ptr<schema::Processor> ToSchemaImpl() const override
    {
        auto schema = std::make_unique<schema::Processor>();
        auto data = schema->mutable_midi_output_data();
        data->set_name(to_utf8(name_));
        
        return schema;
    }
    
    static
    std::unique_ptr<MidiOutput> FromSchemaImpl(schema::Processor const &schema)
    {
        assert(schema.has_midi_output_data());
        
        auto const &data = schema.midi_output_data();
        return std::make_unique<MidiOutputImpl>(to_wstr(data.name()));
    }
    
private:
    String name_;
    std::function<void(MidiOutput *, ProcessInfo const &)> callback_;
    BufferType ref_;
};

//================================================================================================
//================================================================================================
//================================================================================================

std::vector<GraphProcessor::AudioConnectionPtr>
GraphProcessor::Node::GetAudioConnections(BusDirection dir) const
{
    if(dir == BusDirection::kInputSide) {
        return input_audio_connections_;
    } else {
        return output_audio_connections_;
    }
}

std::vector<GraphProcessor::MidiConnectionPtr>
GraphProcessor::Node::GetMidiConnections(BusDirection dir) const
{
    if(dir == BusDirection::kInputSide) {
        return input_midi_connections_;
    } else {
        return output_midi_connections_;
    }
}

template<class List, class F>
auto GetConnectionsToImpl(List const &list, F pred)
{
    List ret;
    std::copy_if(list.begin(), list.end(), std::back_inserter(ret), pred);
    return ret;
}

std::vector<GraphProcessor::AudioConnectionPtr>
GraphProcessor::Node::GetAudioConnectionsTo(BusDirection dir, Node const *target) const
{
    if(dir == BusDirection::kInputSide) {
        return GetConnectionsToImpl(input_audio_connections_, [target](auto x) { return x->upstream_ == target; });
    } else {
        return GetConnectionsToImpl(output_audio_connections_, [target](auto x) { return x->downstream_ == target; });
    }
}

std::vector<GraphProcessor::MidiConnectionPtr>
GraphProcessor::Node::GetMidiConnectionsTo(BusDirection dir, Node const *target) const
{
    if(dir == BusDirection::kInputSide) {
        return GetConnectionsToImpl(input_midi_connections_, [target](auto x) { return x->upstream_ == target; });
    } else {
        return GetConnectionsToImpl(output_midi_connections_, [target](auto x) { return x->downstream_ == target; });
    }
}

template<class List, class F>
bool HasConnectionsToImpl(List const &list, F pred)
{
    return std::any_of(list.begin(), list.end(), pred);
}

bool GraphProcessor::Node::HasAudioConnectionsTo(BusDirection dir, Node const *target) const
{
    if(dir == BusDirection::kInputSide) {
        return HasConnectionsToImpl(input_audio_connections_, [target](auto x) { return x->upstream_ == target; });
    } else {
        return HasConnectionsToImpl(output_audio_connections_, [target](auto x) { return x->downstream_ == target; });
    }
}

bool GraphProcessor::Node::HasMidiConnectionsTo(BusDirection dir, Node const *target) const
{
    if(dir == BusDirection::kInputSide) {
        return HasConnectionsToImpl(input_midi_connections_, [target](auto x) { return x->upstream_ == target; });
    } else {
        return HasConnectionsToImpl(output_midi_connections_, [target](auto x) { return x->downstream_ == target; });
    }
}

bool GraphProcessor::Node::HasConnectionsTo(BusDirection dir, Node const *target) const
{
    return HasAudioConnectionsTo(dir, target) || HasMidiConnectionsTo(dir, target);
}

bool GraphProcessor::Node::IsConnected() const
{
    return  input_audio_connections_.size() > 0
    ||      output_audio_connections_.size() > 0
    ||      input_midi_connections_.size() > 0
    ||      output_midi_connections_.size() > 0;
}

// upstrea から downstream に対して、(間接的にでも) 接続が存在しているかどうか
template<class F>
bool HasPathImpl(GraphProcessor::Node const *upstream,
                 GraphProcessor::Node const *downstream,
                 F get_connections)
{
    auto find_downstream = [get_connections](auto upstream, auto target, auto &hist, auto find_next) {
        for(auto const &conn: get_connections(upstream)) {
            if(conn->downstream_ == target) { return true; }
            // ループを発見。
            if(std::count(hist.begin(), hist.end(), conn) != 0) { return false; }
            
            hist.push_back(conn);
            if(find_next(conn->downstream_, target, hist, find_next)) { return true; }
            hist.pop_back();
        }
        
        return false;
    };
    
    std::vector<std::shared_ptr<GraphProcessor::Connection const>> hist;
    return find_downstream(upstream, downstream, hist, find_downstream);
}

bool GraphProcessor::Node::HasAudioPathTo(Node const *downstream) const
{
    return HasPathImpl(this, downstream, [](auto node) {
        return node->GetAudioConnections(BusDirection::kOutputSide);
    });
}

bool GraphProcessor::Node::HasMidiPathTo(Node const *downstream) const
{
    return HasPathImpl(this, downstream, [](auto node) {
        return node->GetMidiConnections(BusDirection::kOutputSide);
    });
}

bool GraphProcessor::Node::HasPathTo(Node const *downstream) const
{
    auto result = HasPathImpl(this, downstream, [](auto stream) {
        std::vector<ConnectionPtr> tmp;
        auto audio_conns = stream->GetAudioConnections(BusDirection::kOutputSide);
        tmp.insert(tmp.end(), audio_conns.begin(), audio_conns.end());
        auto midi_conns = stream->GetMidiConnections(BusDirection::kOutputSide);
        tmp.insert(tmp.end(), midi_conns.begin(), midi_conns.end());
        return tmp;
    });
    
    return result;
}

class NodeImpl : public GraphProcessor::Node
{
public:
    using RingBuffer = MultiChannelThreadSafeRingBuffer<AudioSample>;
    
    NodeImpl(std::shared_ptr<Processor> processor)
    :   Node(std::move(processor))
    {}
    
    ~NodeImpl()
    {}
    
    void AddConnection(GraphProcessor::AudioConnectionPtr conn, BusDirection dir)
    {
        auto &list = (dir == BusDirection::kInputSide) ? input_audio_connections_ : output_audio_connections_;
        list.push_back(conn);
    }
    
    void AddConnection(GraphProcessor::MidiConnectionPtr conn, BusDirection dir)
    {
        auto &list = (dir == BusDirection::kInputSide) ? input_midi_connections_ : output_midi_connections_;
        list.push_back(conn);
    }
    
    void RemoveConnection(GraphProcessor::ConnectionPtr conn)
    {
        auto remove = [](auto &list, auto conn) {
            auto found = std::find(list.begin(), list.end(), conn);
            if(found != list.end()) { list.erase(found); }
        };
        
        remove(input_audio_connections_, conn);
        remove(output_audio_connections_, conn);
        remove(input_midi_connections_, conn);
        remove(output_midi_connections_, conn);
        
        if(conn->downstream_ == this &&
           dynamic_cast<GraphProcessor::MidiConnection const *>(conn.get()))
        {
            assert(conn->downstream_channel_index_ < input_event_buffers_.GetNumBuffers());
            input_event_buffers_.GetBuffer(conn->downstream_channel_index_)->PopNoteStack();
        }
    }
    
    void OnStartProcessing(double sample_rate, SampleCount block_size)
    {
        // @todo バスの動的な変更をサポートするには、接続情報に持たせているチャンネルインデックスの仕組みを変更し、
        // バスやチャンネルのIDを接続情報に持たせる必要がありそう。
        
        auto const num_audio_inputs = processor_->GetAudioChannelCount(BusDirection::kInputSide);
        auto const num_audio_outputs = processor_->GetAudioChannelCount(BusDirection::kOutputSide);
        
        input_audio_buffer_.resize(num_audio_inputs, block_size);
        output_audio_buffer_.resize(num_audio_outputs, block_size);
        
        auto const num_event_inputs = processor_->GetMidiChannelCount(BusDirection::kInputSide);
        auto const num_event_outputs = processor_->GetMidiChannelCount(BusDirection::kOutputSide);
        
        input_event_buffers_.SetNumBuffers(num_event_inputs);
        output_event_buffers_.SetNumBuffers(num_event_outputs);
        
        processor_->OnStartProcessing(sample_rate, block_size);
    }
    
    void ProcessOnce(TransportInfo const &ti)
    {
        if(processed_) { return; }
        else { processed_ = true; }
        
        input_event_buffers_.ApplyCachedNoteOffs();
        
        output_event_buffers_.Clear();
        input_event_buffers_.Sort();
        
        ProcessInfo pi;
        pi.time_info_ = &ti;
        pi.input_audio_buffer_ = BufferRef<float const > {
            input_audio_buffer_,
            0,
            input_audio_buffer_.channels(),
            0,
            (UInt32)ti.play_.duration_.sample_
        };
        pi.output_audio_buffer_ = BufferRef<float> {
            output_audio_buffer_,
            0,
            output_audio_buffer_.channels(),
            0,
            (UInt32)ti.play_.duration_.sample_
        };
        
        pi.input_event_buffers_ = &input_event_buffers_;
        pi.output_event_buffers_ = &output_event_buffers_;
        
        processor_->Process(pi);
        
        input_event_buffers_.Clear();
    }
    
    void OnStopProcessing()
    {
        processor_->OnStopProcessing();
        input_audio_buffer_ = Buffer<float>();
        output_audio_buffer_ = Buffer<float>();
        input_event_buffers_ = EventBufferList();
        output_event_buffers_ = EventBufferList();
    }
    
    void Clear()
    {
        if(!processed_) { return; }
        
        input_audio_buffer_.fill(0);
        output_audio_buffer_.fill(0);
        input_event_buffers_.Clear();
        output_event_buffers_.Clear();
        processed_ = false;
    }
    
    void AddAudio(BufferRef<float const> src, Int32 channel_to_write_from)
    {
        auto &dest = input_audio_buffer_;
        
        assert(dest.samples() >= src.samples());
        assert(dest.channels() >= src.channels() + channel_to_write_from);

        for(int ch = 0; ch < src.channels(); ++ch) {
            auto const ch_src = src.data()[ch + src.channel_from()] + src.sample_from();
            auto *ch_dest = dest.data()[ch + channel_to_write_from];
            for(int smp = 0; smp < src.samples(); ++smp) {
                ch_dest[smp] += ch_src[smp];
            }
        }
    }
    
    void AddMidi(ArrayRef<ProcessInfo::MidiMessage const> src, UInt32 dest_bus_index)
    {
        auto &dest = input_event_buffers_;
        dest.GetBuffer(dest_bus_index)->AddEvents(src);
    }
    
    std::vector<RingBuffer> channel_delays_;
    Buffer<float> input_audio_buffer_;
    Buffer<float> output_audio_buffer_;
    
    EventBufferList input_event_buffers_;
    EventBufferList output_event_buffers_;
    bool processed_ = false;
};

auto ToNodeImpl(GraphProcessor::Node *node)
{
    assert(node && dynamic_cast<NodeImpl *>(node));
    return static_cast<NodeImpl *>(node);
}

auto ToNodeImpl(GraphProcessor::Node const *node)
{
    assert(node && dynamic_cast<NodeImpl const *>(node));
    return static_cast<NodeImpl const *>(node);
}


//================================================================================================
//================================================================================================
//================================================================================================

struct GraphProcessor::Impl
{
    std::vector<NodePtr> nodes_;
    std::vector<AudioInput *> audio_input_ptrs_;
    std::vector<AudioOutput *> audio_output_ptrs_;
    std::vector<MidiInput *> midi_input_ptrs_;
    std::vector<MidiOutput *> midi_output_ptrs_;
    
    void RegisterIOProcessorIfNeeded(Processor *proc)
    {
        if(auto p = dynamic_cast<AudioInput *>(proc)) {
            AddIOProcessorImpl(audio_input_ptrs_, p);
            return;
        }
        if(auto p = dynamic_cast<AudioOutput *>(proc)) {
            AddIOProcessorImpl(audio_output_ptrs_, p);
            return;
        }
        if(auto p = dynamic_cast<MidiInput *>(proc)) {
            AddIOProcessorImpl(midi_input_ptrs_, p);
            return;
        }
        if(auto p = dynamic_cast<MidiOutput *>(proc)) {
            AddIOProcessorImpl(midi_output_ptrs_, p);
            return;
        }
    }
    
    void UnregisterIOProcessorIfNeeded(Processor const *proc)
    {
        if(auto p = dynamic_cast<AudioInput const *>(proc)) {
            RemoveIOProcessorImpl(audio_input_ptrs_, p);
            return;
        }
        if(auto p = dynamic_cast<AudioOutput const *>(proc)) {
            RemoveIOProcessorImpl(audio_output_ptrs_, p);
            return;
        }
        if(auto p = dynamic_cast<MidiInput const *>(proc)) {
            RemoveIOProcessorImpl(midi_input_ptrs_, p);
            return;
        }
        if(auto p = dynamic_cast<MidiOutput const *>(proc)) {
            RemoveIOProcessorImpl(midi_output_ptrs_, p);
            return;
        }
    }
    
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    
    bool prepared_ = false;
    
    LockFactory lf_;
    
    using FrameProcedure = std::vector<ConnectionPtr>;
    std::shared_ptr<FrameProcedure> frame_procedure_;
    
    std::shared_ptr<FrameProcedure> DuplicateFrameProcedure() const;
    void ReplaceFrameProcedure(std::shared_ptr<FrameProcedure> p);
    std::shared_ptr<FrameProcedure> CreateFrameProcedure() const;
    
    ListenerService<GraphProcessor::Listener> listeners_;
    
private:
    template<class List, class T>
    void AddIOProcessorImpl(List &list, T x) {
        auto found = std::find(list.begin(), list.end(), x);
        assert(found == list.end());
        list.push_back(x);
    }
    
    template<class List, class T>
    void RemoveIOProcessorImpl(List &list, T x) {
        auto found = std::find(list.begin(), list.end(), x);
        assert(found != list.end());
        list.erase(found);
    }
};

std::shared_ptr<GraphProcessor::Impl::FrameProcedure> GraphProcessor::Impl::DuplicateFrameProcedure() const
{
    auto lock = lf_.make_lock();
    auto p = frame_procedure_;
    lock.unlock();
    
    if(!p) { return nullptr; }
    return std::make_shared<FrameProcedure>(*p);
}

void GraphProcessor::Impl::ReplaceFrameProcedure(std::shared_ptr<FrameProcedure> p)
{
    auto lock = lf_.make_lock();
    std::swap(frame_procedure_, p);
    lock.unlock();
}

std::shared_ptr<GraphProcessor::Impl::FrameProcedure> GraphProcessor::Impl::CreateFrameProcedure() const
{
    auto copy = nodes_;
    
    // 接続のあるノードを、begin側が最上流になるように整列
    // 接続のないノードは最下流にあるものとみなす。(これがないと、ソートの条件がStrict Weak Orderingを満たさないので、正しくソートできない）
    std::stable_sort(copy.begin(), copy.end(), [](auto const &lhs, auto const &rhs) {
        if(lhs->IsConnected() == false) { return false; }
        return lhs->HasPathTo(rhs.get());
    });
    
    auto procedure = std::make_shared<FrameProcedure>();
    
    for(auto node: copy) {
        for(auto conn: node->GetAudioConnections(BusDirection::kOutputSide)) {
            procedure->push_back(conn);
        }
        
        for(auto conn: node->GetMidiConnections(BusDirection::kOutputSide)) {
            procedure->push_back(conn);
        }
    }
    
    return procedure;
}

GraphProcessor::GraphProcessor()
:   pimpl_(std::make_unique<Impl>())
{}

GraphProcessor::~GraphProcessor()
{}

GraphProcessor::IListenerService & GraphProcessor::GetListeners()
{
    return pimpl_->listeners_;
}

std::unique_ptr<GraphProcessor::AudioInput>
GraphProcessor::CreateAudioInput(String name, UInt32 channel_index, UInt32 num_channels)
{
    return std::make_unique<AudioInputImpl>(name, channel_index, num_channels);
}

std::unique_ptr<GraphProcessor::AudioOutput>
GraphProcessor::CreateAudioOutput(String name, UInt32 channel_index, UInt32 num_channels)
{
    return std::make_unique<AudioOutputImpl>(name, channel_index, num_channels);
}

std::unique_ptr<GraphProcessor::MidiInput>
GraphProcessor::CreateMidiInput(String name)
{
    return std::make_unique<MidiInputImpl>(name);
}

std::unique_ptr<GraphProcessor::MidiOutput>
GraphProcessor::CreateMidiOutput(String name)
{
    return std::make_unique<MidiOutputImpl>(name);
}

UInt32 GraphProcessor::GetNumAudioInputs() const
{
    return pimpl_->audio_input_ptrs_.size();
}

UInt32 GraphProcessor::GetNumAudioOutputs() const
{
    return pimpl_->audio_output_ptrs_.size();
}

UInt32 GraphProcessor::GetNumMidiInputs() const
{
    return pimpl_->midi_input_ptrs_.size();
}

UInt32 GraphProcessor::GetNumMidiOutputs() const
{
    return pimpl_->midi_output_ptrs_.size();
}

GraphProcessor::AudioInput *  GraphProcessor::GetAudioInput(UInt32 index)
{
    return pimpl_->audio_input_ptrs_[index];
}

GraphProcessor::AudioOutput * GraphProcessor::GetAudioOutput(UInt32 index)
{
    return pimpl_->audio_output_ptrs_[index];
}

GraphProcessor::MidiInput *   GraphProcessor::GetMidiInput(UInt32 index)
{
    return pimpl_->midi_input_ptrs_[index];
}

GraphProcessor::MidiOutput *  GraphProcessor::GetMidiOutput(UInt32 index)
{
    return pimpl_->midi_output_ptrs_[index];
}

GraphProcessor::AudioInput const *  GraphProcessor::GetAudioInput(UInt32 index) const
{
    return pimpl_->audio_input_ptrs_[index];
}

GraphProcessor::AudioOutput const * GraphProcessor::GetAudioOutput(UInt32 index) const
{
    return pimpl_->audio_output_ptrs_[index];
}

GraphProcessor::MidiInput const *   GraphProcessor::GetMidiInput(UInt32 index) const
{
    return pimpl_->midi_input_ptrs_[index];
}

GraphProcessor::MidiOutput const *  GraphProcessor::GetMidiOutput(UInt32 index) const
{
    return pimpl_->midi_output_ptrs_[index];
}

void GraphProcessor::StartProcessing(double sample_rate, SampleCount block_size)
{
    auto lock = pimpl_->lf_.make_lock();
    
    pimpl_->sample_rate_ = sample_rate;
    pimpl_->block_size_ = block_size;
    for(auto &node: pimpl_->nodes_) {
        ToNodeImpl(node.get())->OnStartProcessing(sample_rate, block_size);
    }
    
    pimpl_->prepared_ = true;
}

void GraphProcessor::Process(TransportInfo const &ti)
{
    auto lock = pimpl_->lf_.make_lock();
    
    if(!pimpl_->frame_procedure_) { return; }
    
    for(auto const &conn: *pimpl_->frame_procedure_) {
        ToNodeImpl(conn->upstream_)->Clear();
        ToNodeImpl(conn->downstream_)->Clear();
    }
    
    for(auto const &conn: *pimpl_->frame_procedure_) {
        auto up = ToNodeImpl(conn->upstream_);
        auto down = ToNodeImpl(conn->downstream_);
        
        up->ProcessOnce(ti);
        
        if(auto ac = dynamic_cast<AudioConnection const *>(conn.get())) {
            BufferRef<float const> ref {
                up->output_audio_buffer_,
                ac->upstream_channel_index_,
                ac->num_channels_,
                0,
                (UInt32)ti.play_.duration_.sample_
            };
            down->AddAudio(ref, ac->downstream_channel_index_);
        } else if(auto mc = dynamic_cast<MidiConnection const *>(conn.get())) {
            down->AddMidi(up->output_event_buffers_.GetRef(mc->upstream_channel_index_),
                          mc->downstream_channel_index_);
        } else {
            assert(false);
        }
    }

    //! 下流に接続していないNodeはProcessが呼ばれないので、ここで呼び出すようにする。
    for(auto const &conn: *pimpl_->frame_procedure_) {
        ToNodeImpl(conn->downstream_)->ProcessOnce(ti);
    }
    
    for(auto const &conn: *pimpl_->frame_procedure_) {
        auto node = ToNodeImpl(conn->downstream_);
        if(auto p = dynamic_cast<AudioOutputImpl *>(node->GetProcessor().get())) {
            p->ProcessPostFader();
        }
    }
}

void GraphProcessor::StopProcessing()
{
    auto lock = pimpl_->lf_.make_lock();
    
    for(auto node: pimpl_->nodes_) {
        ToNodeImpl(node.get())->OnStopProcessing();
    }
    
    pimpl_->prepared_ = false;
}

//! don't call this function on the realtime thread.
//! Nothing to do if the processor is added aleady.
GraphProcessor::NodePtr GraphProcessor::AddNode(std::shared_ptr<Processor> processor)
{
    assert(processor);
    
    auto const found = std::find_if(pimpl_->nodes_.begin(), pimpl_->nodes_.end(),
                                    [p = processor.get()](auto const &x) {
                                        return x->GetProcessor().get() == p;
                                    });
    
    if(found != pimpl_->nodes_.end()) { return *found; }
    
    auto node = std::make_shared<NodeImpl>(processor);
    
    pimpl_->nodes_.push_back(node);
    pimpl_->RegisterIOProcessorIfNeeded(node->GetProcessor().get());
    
    if(pimpl_->prepared_) {
        node->OnStartProcessing(pimpl_->sample_rate_, pimpl_->block_size_);
    }
    
    pimpl_->listeners_.Invoke([&](Listener *li) {
        li->OnAfterNodeIsAdded(node.get());
    });
    return node;
}

//! don't call this function on the realtime thread.
//! Nothing to do if the processor is not added.
std::shared_ptr<Processor> GraphProcessor::RemoveNode(std::shared_ptr<Node const> node)
{
    return RemoveNode(node.get());
}

std::shared_ptr<Processor> GraphProcessor::RemoveNode(Node const *node)
{
    assert(node);
    
    bool const should_stop_processing = pimpl_->prepared_;
    
    auto const found = std::find_if(pimpl_->nodes_.begin(), pimpl_->nodes_.end(),
                                    [node](auto &elem) { return elem.get() == node; }
                                    );
    
    if(found == pimpl_->nodes_.end()) { return nullptr; }
    
    pimpl_->listeners_.Invoke([&](Listener *li) {
        li->OnBeforeNodeIsRemoved(found->get());
    });
    
    Disconnect(found->get());
    
    auto moved = std::move(*found);
    pimpl_->nodes_.erase(found);
    pimpl_->UnregisterIOProcessorIfNeeded(moved->GetProcessor().get());
    
    if(should_stop_processing) {
        ToNodeImpl(moved.get())->OnStopProcessing();
    }
    
    return moved->GetProcessor();
}

GraphProcessor::NodePtr GraphProcessor::GetNodeOf(Processor const *processor) const
{
    auto found = std::find_if(pimpl_->nodes_.begin(),
                              pimpl_->nodes_.end(),
                              [processor](auto node) { return node->GetProcessor().get() == processor; }
                              );
    
    if(found != pimpl_->nodes_.end()) {
        return *found;
    } else {
        return nullptr;
    }
}

std::vector<GraphProcessor::NodePtr> GraphProcessor::GetNodes() const
{
    return pimpl_->nodes_;
}

bool GraphProcessor::ConnectAudio(Node *upstream,
                  Node *downstream,
                  UInt32 upstream_channel_index,
                  UInt32 downstream_channel_index,
                  UInt32 num_channels)
{
    assert(num_channels >= 1);
    assert(upstream_channel_index + num_channels <= upstream->GetProcessor()->GetAudioChannelCount(BusDirection::kOutputSide));
    assert(downstream_channel_index + num_channels <= downstream->GetProcessor()->GetAudioChannelCount(BusDirection::kInputSide));
    
    //! loop-back connection is not supported yet.
    if(downstream->HasPathTo(upstream)) { return false; }
    
    //! 要求されたチャンネルと重なっている接続がすでに存在しているかどうかをチェック
    auto const list = upstream->GetAudioConnections(BusDirection::kOutputSide);
    
    bool const has_overlapped_connection = std::any_of(list.begin(), list.end(), [&](auto conn) {
        if(conn->upstream_ != upstream) { return false; }
        if(conn->downstream_ != downstream) { return false; }
        
        // 区間A, Bが重なっているかどうか
        auto intersect = [](UInt32 a1, UInt32 a2, UInt32 b1, UInt32 b2) {
            assert(a1 < a2 && b1 < b2);
            return a1 < b2 && b1 < a2;
        };
        
        if(intersect(conn->upstream_channel_index_,
                     conn->upstream_channel_index_ + conn->num_channels_,
                     upstream_channel_index,
                     upstream_channel_index + num_channels))
        { return true; }
        
        if(intersect(conn->downstream_channel_index_,
                     conn->downstream_channel_index_ + conn->num_channels_,
                     downstream_channel_index,
                     downstream_channel_index + num_channels))
        { return true; }
        
        return false;
    });
    
    if(has_overlapped_connection) { return false; }
    
    auto c = std::make_shared<AudioConnection>(upstream, downstream,
                                               upstream_channel_index, downstream_channel_index,
                                               num_channels);

    ToNodeImpl(upstream)->AddConnection(c, BusDirection::kOutputSide);
    ToNodeImpl(downstream)->AddConnection(c, BusDirection::kInputSide);
    
    auto new_procedure = pimpl_->CreateFrameProcedure();
    pimpl_->ReplaceFrameProcedure(new_procedure);
    return true;
}

bool GraphProcessor::ConnectMidi(Node *upstream, Node *downstream,
                                 UInt32 upstream_channel_index,
                                 UInt32 downstream_channel_index)
{
    assert(upstream_channel_index < upstream->GetProcessor()->GetMidiChannelCount(BusDirection::kOutputSide));
    assert(downstream_channel_index < downstream->GetProcessor()->GetMidiChannelCount(BusDirection::kInputSide));
    
    //! loop-back connection is not supported yet.
    if(downstream->HasPathTo(upstream)) { return false; }
    
    //! 要求されたチャンネルと重なっている接続がすでに存在しているかどうかをチェック
    auto const list = upstream->GetMidiConnections(BusDirection::kOutputSide);
    
    bool const has_same_connection = std::any_of(list.begin(), list.end(), [&](auto conn) {
        if(conn->upstream_ != upstream) { return false; }
        if(conn->downstream_ != downstream) { return false; }
        if(conn->upstream_channel_index_ != upstream_channel_index) { return false; }
        if(conn->downstream_channel_index_ != downstream_channel_index) { return false; }

        return true;
    });
    
    if(has_same_connection) { return false; }
    
    auto c = std::make_shared<MidiConnection>(upstream, downstream,
                                               upstream_channel_index, downstream_channel_index);
    
    ToNodeImpl(upstream)->AddConnection(c, BusDirection::kOutputSide);
    ToNodeImpl(downstream)->AddConnection(c, BusDirection::kInputSide);
    
    auto new_procedure = pimpl_->CreateFrameProcedure();
    pimpl_->ReplaceFrameProcedure(new_procedure);
    return true;
}

void RemoveConnection(GraphProcessor::ConnectionPtr conn)
{
    auto nup = dynamic_cast<NodeImpl *>(conn->upstream_);
    auto ndown = dynamic_cast<NodeImpl *>(conn->downstream_);
    assert(nup && ndown);
    
    nup->RemoveConnection(conn);
    ndown->RemoveConnection(conn);
}

//! このノードとの接続をすべて解除する
bool GraphProcessor::Disconnect(Node const *node)
{
    if(auto procedure = pimpl_->DuplicateFrameProcedure()) {
        auto removed = std::remove_if(procedure->begin(), procedure->end(),
                                      [node](auto conn) {
                                          return (conn->upstream_ == node || conn->downstream_ == node);
                                      });
        if(removed != procedure->end()) {
            procedure->erase(removed, procedure->end());
            pimpl_->ReplaceFrameProcedure(procedure);
        }
    }
    
    auto found = std::find_if(pimpl_->nodes_.begin(), pimpl_->nodes_.end(),
                              [node](auto const &x) { return x.get() == node; });
    
    if(found == pimpl_->nodes_.end()) { return false; }
    
    auto mutable_node = found->get();
    
    auto remove_connection = [](auto list) -> UInt32 {
        UInt32 num = list.size();
        std::for_each(list.begin(), list.end(), RemoveConnection);
        return num;
    };
    
    UInt32 num = 0;
    num += remove_connection(mutable_node->GetAudioConnections(BusDirection::kInputSide));
    num += remove_connection(mutable_node->GetAudioConnections(BusDirection::kOutputSide));
    num += remove_connection(mutable_node->GetMidiConnections(BusDirection::kInputSide));
    num += remove_connection(mutable_node->GetMidiConnections(BusDirection::kOutputSide));
    
    return num != 0;
}

//! この接続を解除する
bool GraphProcessor::Disconnect(ConnectionPtr conn)
{
    if(auto procedure = pimpl_->DuplicateFrameProcedure()) {
        auto removed = std::remove(procedure->begin(), procedure->end(), conn);
        if(removed != procedure->end()) {
            procedure->erase(removed, procedure->end());
            pimpl_->ReplaceFrameProcedure(procedure);
        }
    }
    
    auto as = conn->upstream_->GetAudioConnections(BusDirection::kOutputSide);
    auto ms = conn->upstream_->GetMidiConnections(BusDirection::kOutputSide);
    
    if(std::count(as.begin(), as.end(), conn) == 0
       && std::count(ms.begin(), ms.end(), conn) == 0)
    {
        return false;
    }
    
    RemoveConnection(conn);
    return true;
}

std::unique_ptr<schema::NodeGraph> GraphProcessor::ToSchema() const
{
    auto p = std::make_unique<schema::NodeGraph>();
    
    for(auto const &node: pimpl_->nodes_) {
        auto new_node = p->add_nodes();
        new_node->set_id(node->GetID());
        
        auto new_proc = node->GetProcessor()->ToSchema();
        new_node->set_allocated_processor(new_proc.release());
        
        for(auto const &ac: node->GetAudioConnections(BusDirection::kOutputSide)) {
            auto conn = p->add_connections();
            conn->set_type(schema::NodeGraph_Connection_Type_kAudio);
            conn->set_upstream_id(node->GetID());
            conn->set_downstream_id(ac->downstream_->GetID());
            conn->set_upstream_channel_index(ac->upstream_channel_index_);
            conn->set_downstream_channel_index(ac->downstream_channel_index_);
        }
        
        for(auto const &ac: node->GetMidiConnections(BusDirection::kOutputSide)) {
            auto conn = p->add_connections();
            conn->set_type(schema::NodeGraph_Connection_Type_kEvent);
            conn->set_upstream_id(node->GetID());
            conn->set_downstream_id(ac->downstream_->GetID());
            conn->set_upstream_channel_index(ac->upstream_channel_index_);
            conn->set_downstream_channel_index(ac->downstream_channel_index_);
        }
    }
    
    return p;
}

std::unique_ptr<GraphProcessor> GraphProcessor::FromSchema(schema::NodeGraph const &schema)
{
    auto p = std::make_unique<GraphProcessor>();
    
    auto objects = ProjectObjectTable::GetInstance();
    assert(objects);
    
    auto &node_table = objects->nodes_;
    
    for(auto const &node: schema.nodes()) {
        if(node.has_processor() == false) { continue; }

        auto &np = node.processor();
        std::unique_ptr<Processor> proc;
        
        if(np.has_audio_input_data()) {
            proc = AudioInputImpl::FromSchemaImpl(np);
        } else if(np.has_audio_output_data()) {
            proc = AudioOutputImpl::FromSchemaImpl(np);
        } else if(np.has_midi_input_data()) {
            proc = MidiInputImpl::FromSchemaImpl(np);
        } else if(np.has_midi_output_data()) {
            proc = MidiOutputImpl::FromSchemaImpl(np);
        } else {
            proc = Processor::FromSchema(node.processor());
        }

        NodePtr new_node = p->AddNode(std::move(proc));
        node_table.Register(node.id(), new_node.get());
    }
    
    for(auto const &conn: schema.connections()) {
        auto up_node = node_table.Find(conn.upstream_id());
        auto down_node = node_table.Find(conn.downstream_id());
        
        if(!up_node || !down_node) { continue; }
        
        auto up_proc = up_node->GetProcessor().get();
        auto down_proc = down_node->GetProcessor().get();
        
        if(conn.type() == schema::NodeGraph_Connection_Type_kAudio) {
            auto up_ch = conn.upstream_channel_index();
            auto down_ch = conn.downstream_channel_index();
            
            if(up_ch >= up_proc->GetAudioChannelCount(BusDirection::kOutputSide) ||
               down_ch >= down_proc->GetAudioChannelCount(BusDirection::kInputSide))
            {
                continue;
            }
            
            p->ConnectAudio(up_node, down_node, up_ch, down_ch);
        } else if(conn.type() == schema::NodeGraph_Connection_Type_kEvent) {
            auto up_ch = conn.upstream_channel_index();
            auto down_ch = conn.downstream_channel_index();
            
            if(up_ch >= up_proc->GetMidiChannelCount(BusDirection::kOutputSide) ||
               down_ch >= down_proc->GetMidiChannelCount(BusDirection::kInputSide))
            {
                continue;
            }
            
            p->ConnectMidi(up_node, down_node, up_ch, down_ch);
        }
    }
    
    return p;
}

NS_HWM_END
