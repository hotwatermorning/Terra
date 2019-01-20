#include "./GraphProcessor.hpp"

NS_HWM_BEGIN

class AudioInputImpl : public GraphProcessor::AudioInput
{
public:
    AudioInputImpl(String name, UInt32 num_channels,
                   std::function<void(AudioInput *, ProcessInfo const &)> callback)
    :   name_(name)
    ,   num_channels_(num_channels)
    ,   callback_(callback)
    {
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
    
    void OnStartProcessing(double sample_rate, SampleCount block_size) override
    {
    }
    
    void Process(ProcessInfo &pi) override
    {
        callback_(this, pi);
        auto dest = pi.output_audio_buffer_;
        
        auto channels = std::min<int>(dest.channels(), ref_.channels());
        for(int ch = 0; ch < channels; ++ch) {
            auto ch_src = ref_.get_channel_data(ch);
            auto ch_dest = dest.get_channel_data(ch);
            std::copy_n(ch_src, pi.time_info_->GetSmpDuration(), ch_dest);
        }
    }
    
    void OnStopProcessing() override
    {}
    
private:
    String name_;
    UInt32 num_channels_;
    std::function<void(AudioInput *, ProcessInfo const &)> callback_;
    BufferRef<float const> ref_;
};

class AudioOutputImpl : public GraphProcessor::AudioOutput
{
public:
    AudioOutputImpl(String name, UInt32 num_channels,
                    std::function<void(AudioOutput *, ProcessInfo const &)> callback)
    :   name_(name)
    ,   num_channels_(num_channels)
    ,   callback_(callback)
    {
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
    
    void OnStartProcessing(double sample_rate, SampleCount block_size) override
    {}
    
    void Process(ProcessInfo &pi) override
    {
        ref_ = pi.input_audio_buffer_;
        callback_(this, pi);
    }
    
    void OnStopProcessing() override
    {}
    
private:
    String name_;
    UInt32 num_channels_;
    std::function<void(AudioOutput *, ProcessInfo const &)> callback_;
    BufferRef<float const> ref_;
};

class MidiInputImpl : public GraphProcessor::MidiInput
{
public:
    MidiInputImpl(String name,
                  std::function<void(MidiInput *, ProcessInfo const &)> callback)
    :   name_(name)
    ,   callback_(callback)
    {
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
    
    void OnStartProcessing(double sample_rate, SampleCount block_size) override
    {
        
    }
    
    void Process(ProcessInfo &pi) override
    {
        callback_(this, pi);
        
        auto &dest = pi.output_midi_buffer_;

        assert(ref_.size() <= dest.buffer_.size());
        std::copy_n(ref_.data(), ref_.size(), dest.buffer_.begin());
        dest.num_used_ = ref_.size();
    }
    
    void OnStopProcessing() override
    {}
    
private:
    String name_;
    std::function<void(MidiInput *, ProcessInfo const &)> callback_;
    BufferType ref_;
};

class MidiOutputImpl : public GraphProcessor::MidiOutput
{
public:
    MidiOutputImpl(String name,
                    std::function<void(MidiOutput *, ProcessInfo const &)> callback)
    :   name_(name)
    ,   callback_(callback)
    {
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
    
    void OnStartProcessing(double sample_rate, SampleCount block_size) override
    {}
    
    void Process(ProcessInfo &pi) override
    {
        ref_ = BufferType {
            pi.output_midi_buffer_.buffer_.begin(),
            pi.output_midi_buffer_.buffer_.begin() + pi.output_midi_buffer_.num_used_
        };
        callback_(this, pi);
    }
    
    void OnStopProcessing() override
    {}
    
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
auto GetConnectionsTo(List const &list, BusDirection dir, F pred)
{
    List ret;
    
    if(dir == BusDirection::kInputSide) {
        std::copy_if(list.begin(), list.end(), std::back_inserter(ret), pred);
    } else {
        std::copy_if(list.begin(), list.end(), std::back_inserter(ret), pred);
    }
    
    return ret;
}

std::vector<GraphProcessor::AudioConnectionPtr>
GraphProcessor::Node::GetAudioConnectionsTo(BusDirection dir, Node const *target) const
{
    if(dir == BusDirection::kInputSide) {
        return GetConnectionsTo(input_audio_connections_, dir, [target](auto x) { return x->upstream_ == target; });
    } else {
        return GetConnectionsTo(output_audio_connections_, dir, [target](auto x) { return x->downstream_ == target; });
    }
}

std::vector<GraphProcessor::MidiConnectionPtr>
GraphProcessor::Node::GetMidiConnectionsTo(BusDirection dir, Node const *target) const
{
    if(dir == BusDirection::kInputSide) {
        return GetConnectionsTo(input_midi_connections_, dir, [target](auto x) { return x->upstream_ == target; });
    } else {
        return GetConnectionsTo(output_midi_connections_, dir, [target](auto x) { return x->downstream_ == target; });
    }
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
    return HasPathImpl(this, downstream, [](auto stream) {
        return stream->GetMidiConnections(BusDirection::kOutputSide);
    });
}

bool GraphProcessor::Node::HasPathTo(Node const *downstream) const
{
    return HasAudioPathTo(downstream) || HasMidiPathTo(downstream);
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
    }
    
    void OnStartProcessing(double sample_rate, SampleCount block_size)
    {
        auto const num_inputs = processor_->GetAudioChannelCount(BusDirection::kInputSide);
        auto const num_outputs = processor_->GetAudioChannelCount(BusDirection::kOutputSide);
        
        input_audio_buffer_.resize(num_inputs, block_size);
        output_audio_buffer_.resize(num_outputs, block_size);
        input_midi_buffer_.reserve(block_size);
        output_midi_buffer_.reserve(block_size);
        
        processor_->OnStartProcessing(sample_rate, block_size);
    }
    
    void ProcessOnce(TransportInfo const &ti)
    {
        if(processed_) { return; }
        else { processed_ = true; }
        
        ProcessInfo pi;
        pi.time_info_ = &ti;
        pi.input_audio_buffer_ = BufferRef<float const > {
            input_audio_buffer_,
            0,
            input_audio_buffer_.channels(),
            0,
            (UInt32)ti.GetSmpDuration()
        };
        pi.output_audio_buffer_ = BufferRef<float> {
            output_audio_buffer_,
            0,
            output_audio_buffer_.channels(),
            0,
            (UInt32)ti.GetSmpDuration()
        };
        
        pi.input_midi_buffer_ = { input_midi_buffer_, (UInt32)input_midi_buffer_.size() };
        output_midi_buffer_.resize(output_midi_buffer_.capacity());
        pi.output_midi_buffer_ = { output_midi_buffer_, 0 };
        
        processor_->Process(pi);
        
        input_midi_buffer_.clear();
    }
    
    void OnStopProcessing()
    {
        processor_->OnStopProcessing();
        input_audio_buffer_ = Buffer<float>();
        output_audio_buffer_ = Buffer<float>();
        input_midi_buffer_ = MidiMessageList();
        output_midi_buffer_ = MidiMessageList();
    }
    
    void Clear()
    {
        if(!processed_) { return; }
        
        input_audio_buffer_.fill(0);
        output_audio_buffer_.fill(0);
        input_midi_buffer_.clear();
        output_midi_buffer_.clear();
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
    
    void AddMidi(ArrayRef<ProcessInfo::MidiMessage> const &src)
    {
        auto &dest = input_midi_buffer_;
        std::copy(src.begin(), src.end(), std::back_inserter(dest));
    }
    
    std::vector<RingBuffer> channel_delays_;
    Buffer<float> input_audio_buffer_;
    Buffer<float> output_audio_buffer_;
    using MidiMessageList = std::vector<ProcessInfo::MidiMessage>;
    MidiMessageList input_midi_buffer_;
    MidiMessageList output_midi_buffer_;
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
    
    void AddIONode(AudioInput *p) { AddIONodeImpl(audio_input_ptrs_, p); }
    void AddIONode(AudioOutput *p) { AddIONodeImpl(audio_output_ptrs_, p); }
    void AddIONode(MidiInput *p) { AddIONodeImpl(midi_input_ptrs_, p); }
    void AddIONode(MidiOutput *p) { AddIONodeImpl(midi_output_ptrs_, p); }
    
    void RemoveIONode(AudioInput const *p) { RemoveIONodeImpl(audio_input_ptrs_, p); }
    void RemoveIONode(AudioOutput const *p) { RemoveIONodeImpl(audio_output_ptrs_, p); }
    void RemoveIONode(MidiInput const *p) { RemoveIONodeImpl(midi_input_ptrs_, p); }
    void RemoveIONode(MidiOutput const *p) { RemoveIONodeImpl(midi_output_ptrs_, p); }
    
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    
    bool prepared_ = false;
    
    LockFactory lf_;
    
    using FrameProcedure = std::vector<ConnectionPtr>;
    std::shared_ptr<FrameProcedure> frame_procedure_;
    
    std::shared_ptr<FrameProcedure> DuplicateFrameProcedure() const;
    void ReplaceFrameProcedure(std::shared_ptr<FrameProcedure> p);
    std::shared_ptr<FrameProcedure> CreateFrameProcedure() const;
    
private:
    template<class List, class T>
    void AddIONodeImpl(List &list, T x) {
        auto found = std::find(list.begin(), list.end(), x);
        assert(found == list.end());
        list.push_back(x);
    }
    
    template<class List, class T>
    void RemoveIONodeImpl(List &list, T x) {
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
    
    // begin側が最上流になる
    std::stable_sort(copy.begin(), copy.end(), [](auto const &lhs, auto const &rhs) {
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

GraphProcessor::AudioInput *
GraphProcessor::AddAudioInput(String name, UInt32 num_channels,
                              std::function<void(AudioInput *, ProcessInfo const &)> callback)
{
    auto p = std::make_shared<AudioInputImpl>(name, num_channels, callback);
    AddNode(p);
    pimpl_->AddIONode(p.get());
    return p.get();
}

GraphProcessor::AudioOutput *
GraphProcessor::AddAudioOutput(String name, UInt32 num_channels,
                               std::function<void(AudioOutput *, ProcessInfo const &)> callback)
{
    auto p = std::make_shared<AudioOutputImpl>(name, num_channels, callback);
    AddNode(p);
    pimpl_->AddIONode(p.get());
    return p.get();
}

GraphProcessor::MidiInput *
GraphProcessor::AddMidiInput(String name,
                             std::function<void(MidiInput *, ProcessInfo const &)> callback)
{
    auto p = std::make_shared<MidiInputImpl>(name, callback);
    AddNode(p);
    pimpl_->AddIONode(p.get());
    return p.get();
}

GraphProcessor::MidiOutput *
GraphProcessor::AddMidiOutput(String name,
                              std::function<void(MidiOutput *, ProcessInfo const &)> callback)
{
    auto p = std::make_shared<MidiOutputImpl>(name, callback);
    AddNode(p);
    pimpl_->AddIONode(p.get());
    return p.get();
}

void GraphProcessor::RemoveAudioInput(AudioInput const *p)
{
    RemoveNode(p);
    pimpl_->RemoveIONode(p);
}

void GraphProcessor::RemoveAudioOutput(AudioOutput const *p)
{
    RemoveNode(p);
    pimpl_->RemoveIONode(p);
}

void GraphProcessor::RemoveMidiInput(MidiInput const *p)
{
    RemoveNode(p);
    pimpl_->RemoveIONode(p);
}

void GraphProcessor::RemoveMidiOutput(MidiOutput const *p)
{
    RemoveNode(p);
    pimpl_->RemoveIONode(p);
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
                (UInt32)ti.GetSmpDuration()
            };
            down->AddAudio(ref, ac->downstream_channel_index_);
        } else if(auto mc = dynamic_cast<MidiConnection const *>(conn.get())) {
            down->AddMidi(up->output_midi_buffer_);
        } else {
            assert(false);
        }
    }

    //! 下流に接続していないNodeはProcessが呼ばれないので、ここで呼び出すようにする。
    for(auto const &conn: *pimpl_->frame_procedure_) {
        ToNodeImpl(conn->downstream_)->ProcessOnce(ti);
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
    auto const found = std::find_if(pimpl_->nodes_.begin(), pimpl_->nodes_.end(),
                                    [p = processor.get()](auto const &x) {
                                        return x->GetProcessor().get() == p;
                                    });
    
    if(found != pimpl_->nodes_.end()) { return *found; }
    
    auto node = std::make_shared<NodeImpl>(processor);
    pimpl_->nodes_.push_back(node);
    
    if(pimpl_->prepared_) {
        node->OnStartProcessing(pimpl_->sample_rate_, pimpl_->block_size_);
    }
    
    return node;
}

//! don't call this function on the realtime thread.
//! Nothing to do if the processor is not added.
std::shared_ptr<Processor> GraphProcessor::RemoveNode(Processor const *processor)
{
    assert(processor);
    
    bool const should_stop_processing = pimpl_->prepared_;
    
    auto const found = std::find_if(pimpl_->nodes_.begin(), pimpl_->nodes_.end(),
                                    [p = processor](auto const &x) { return x->GetProcessor().get() == p; }
                                    );
    
    if(found == pimpl_->nodes_.end()) { return nullptr; }
    
    Disconnect(found->get());
    
    auto node = *found;
    pimpl_->nodes_.erase(found);
    
    if(should_stop_processing) {
        ToNodeImpl(node.get())->OnStopProcessing();
    }
    
    return node->GetProcessor();
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

NS_HWM_END
