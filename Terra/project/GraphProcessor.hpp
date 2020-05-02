#pragma once
#include "../plugin/vst3/Vst3Plugin.hpp"
#include "../misc/ThreadSafeRingBuffer.hpp"
#include "../misc/LockFactory.hpp"
#include "../transport/TransportInfo.hpp"
#include "../processor/Processor.hpp"
#include "./Sequence.hpp"

NS_HWM_BEGIN

class GraphProcessor
:   public Processor
{
public:
    class Node;
    using NodePtr = std::shared_ptr<Node>;
    class Connection;
    class AudioConnection;
    class MidiConnection;
    using ConnectionPtr = std::shared_ptr<Connection const>;
    using AudioConnectionPtr = std::shared_ptr<AudioConnection const>;
    using MidiConnectionPtr = std::shared_ptr<MidiConnection const>;
    
    struct Listener : public IListenerBase
    {
    protected:
        Listener() {}
    public:
        virtual void OnAfterNodeIsAdded(Node *node) {};
        virtual void OnBeforeNodeIsRemoved(Node *node) {};
    };
    
    using IListenerService = IListenerService<Listener>;
    
public:
    GraphProcessor();
    ~GraphProcessor();
    
    IListenerService & GetListeners();
    
    class AudioInput : public Processor {
    public:
        //! The callback must be set before `OnStartProcessing()`
        virtual
        void SetCallback(std::function<void(AudioInput *, ProcessInfo const &)> callback) = 0;
        
        virtual
        void SetData(BufferRef<float const> buf) = 0;

        //! represend a start channel index belongs to this processor.
        virtual
        UInt32 GetChannelIndex() const = 0;
    };
    
    class AudioOutput : public Processor {
    public:
        //! The callback must be set before `OnStartProcessing()`
        virtual
        void SetCallback(std::function<void(AudioOutput *, ProcessInfo const &)> callback) = 0;
        
        virtual
        BufferRef<float const> GetData() const = 0;
        
        //! represend a start channel index belongs to this processor.
        virtual
        UInt32 GetChannelIndex() const = 0;
    };
    
    class MidiInput : public Processor {
    public:
        //! The callback must be set before `OnStartProcessing()`
        virtual
        void SetCallback(std::function<void(MidiInput *, ProcessInfo const &)> callback) = 0;
        
        using BufferType = ArrayRef<ProcessInfo::MidiMessage const>;
        
        virtual
        void SetData(BufferType buf) = 0;
    };
    
    class MidiOutput : public Processor {
    public:
        //! The callback must be set before `OnStartProcessing()`
        virtual
        void SetCallback(std::function<void(MidiOutput *, ProcessInfo const &)> callback) = 0;
        
        using BufferType = ArrayRef<ProcessInfo::MidiMessage const>;
        
        virtual
        BufferType GetData() const = 0;
    };
    
public:
    //! @param
    static
    std::unique_ptr<AudioInput>  CreateAudioInput(String name, UInt32 channel_index, UInt32 num_channels);

    static
    std::unique_ptr<AudioOutput> CreateAudioOutput(String name, UInt32 channel_index, UInt32 num_channels);

    static
    std::unique_ptr<MidiInput>   CreateMidiInput(String name);

    static
    std::unique_ptr<MidiOutput>  CreateMidiOutput(String name);
    
    UInt32 GetNumAudioInputs() const;
    UInt32 GetNumAudioOutputs() const;
    UInt32 GetNumMidiInputs() const;
    UInt32 GetNumMidiOutputs() const;
    
    AudioInput *  GetAudioInput(UInt32 index);
    AudioOutput * GetAudioOutput(UInt32 index);
    MidiInput *   GetMidiInput(UInt32 index);
    MidiOutput *  GetMidiOutput(UInt32 index);
    
    AudioInput const *  GetAudioInput(UInt32 index) const;
    AudioOutput const * GetAudioOutput(UInt32 index) const;
    MidiInput const *   GetMidiInput(UInt32 index) const;
    MidiOutput const *  GetMidiOutput(UInt32 index) const;
    
    void StartProcessing(double sample_rate, SampleCount block_size);
    void Process(SampleCount num_samples);
    void StopProcessing();
    
    class Connection
    {
    protected:
        Connection(Node *upstream, Node *downstream,
                   UInt32 upstream_channel_index, UInt32 downstream_channel_index)
        :   upstream_channel_index_(upstream_channel_index)
        ,   downstream_channel_index_(downstream_channel_index)
        ,   upstream_(upstream)
        ,   downstream_(downstream)
        {}
        
    public:
        virtual ~Connection() {}
        
        UInt32 upstream_channel_index_ = 0;
        UInt32 downstream_channel_index_ = 0;
        Node *upstream_ = nullptr;
        Node *downstream_ = nullptr;
    };
    
    class AudioConnection : public Connection
    {
    public:
        AudioConnection(Node *upstream, Node *downstream,
                        UInt32 upstream_channel_index, UInt32 downstream_channel_index,
                        UInt32 num_channels)
        :   Connection(upstream, downstream, upstream_channel_index, downstream_channel_index)
        ,   num_channels_(num_channels)
        {}
        
        UInt32 num_channels_; // 1 means mono
    };
    
    class MidiConnection : public Connection
    {
    public:
        MidiConnection(Node *upstream, Node *downstream,
                       UInt32 upstream_channel_index, UInt32 downstream_channel_index)
        :   Connection(upstream, downstream, upstream_channel_index, downstream_channel_index)
        {}
    };
    
    class Node
    {
    protected:
        Node();
        
    public:
        virtual ~Node();
        
        UInt64 GetID() const { return reinterpret_cast<UInt64>(this); }

        virtual
        std::shared_ptr<Processor> GetProcessor() const = 0;
        
        //! get audio connection list to the specified direction
        virtual
        std::vector<AudioConnectionPtr> GetAudioConnections(BusDirection dir) const = 0;
        
        //! get midi connection list to the specified direction
        virtual
        std::vector<MidiConnectionPtr> GetMidiConnections(BusDirection dir) const = 0;
        
        //! get the audio connection to the specified target.
        /*! @return return the audio connection if found, nullptr otherwise.
         */
        virtual
        std::vector<AudioConnectionPtr> GetAudioConnectionsTo(BusDirection dir, Node const *target) const = 0;
        //! get the midi connection to the specified target.
        /*! @return return the midi connection if found, nullptr otherwise.
         */
        virtual
        std::vector<MidiConnectionPtr> GetMidiConnectionsTo(BusDirection dir, Node const *target) const = 0;
        
        //! @return true if this node is connected to the target directly with any audio connections.
        virtual
        bool HasAudioConnectionsTo(BusDirection dir, Node const *target) const = 0;
        
        //! @return true if this node is connected to the target directly with any midi connections.
        virtual
        bool HasMidiConnectionsTo(BusDirection dir, Node const *target) const = 0;
        
        //! @return true if this node is connected to the target directly with any connections.
        virtual
        bool HasConnectionsTo(BusDirection dir, Node const *target) const = 0;
        
        //! @return true if this node is connected to any targets.
        virtual
        bool IsConnected() const = 0;

        //! check that this node is connected directly or indirectly to the specified node.
        //! @return true if the any downstream audio connections reach to the specified node.
        virtual
        bool HasAudioPathTo(Node const *downstream) const = 0;
        
        //! check that this node is connected directly or indirectly to the specified node.
        //! @return true if the any downstream midi connections reach to the specified node.
        virtual
        bool HasMidiPathTo(Node const *downstream) const = 0;
        
        //! check that this node is connected directly or indirectly to the specified node.
        //! @return true if the any downstream connections reach to the specified node.
        virtual
        bool HasPathTo(Node const *downstream) const = 0;
    };
    
    //! don't call this function on the realtime thread.
    //! Nothing to do if the processor is added aleady.
    NodePtr AddNode(std::shared_ptr<Processor> processor);
    
    //! don't call this function on the realtime thread.
    //! Nothing to do if the processor is not added.
    std::shared_ptr<Processor> RemoveNode(std::shared_ptr<Node const> node);
    
    //! don't call this function on the realtime thread.
    //! Nothing to do if the processor is not added.
    std::shared_ptr<Processor> RemoveNode(Node const *node);
    
    NodePtr GetNodeOf(Processor const *processor) const;
    std::vector<NodePtr> GetNodes() const;
    
    bool ConnectAudio(Node *upstream,
                      Node *downstream,
                      UInt32 upstream_channel_index,
                      UInt32 downstream_channel_index,
                      UInt32 num_channels = 1);
    
    bool ConnectMidi(Node *upstream,
                     Node *downstream,
                     UInt32 upstream_channel_index,
                     UInt32 downstream_channel_index);
    
    //! このノードに繋がる接続をすべて切断する
    /*! @return 接続を切断した場合はtrueが帰る。接続が一つも見つからないために何もしなかった場合はfalseが帰る。
     */
    bool Disconnect(Node const *stream);
    
    //! この接続を切断する
    /*! @return 接続を切断した場合はtrueが帰る。接続が一つも見つからないために何もしなかった場合はfalseが帰る。
     */
    bool Disconnect(ConnectionPtr conn);
    
    std::unique_ptr<schema::NodeGraph> ToSchema() const;
    
    static
    std::unique_ptr<GraphProcessor> FromSchema(schema::NodeGraph const &schema);
    
//    virtual
//    SampleCount GetLatencySample() const { return 0; }
//
//    //! オーディオ入出力チャンネル数
//    virtual
//    UInt32 GetAudioChannelCount(BusDirection dir) const { return 0; }
//
//    //! Midi入出力チャンネル数
//    /*! ここでいうチャンネルは、Midiメッセージのチャンネルではなく、
//     *  VST3のEventBusのインデックスを表す。
//     */
//    virtual
//    UInt32 GetMidiChannelCount(BusDirection dir) const { return 0; }
    
    bool HasEditor() const override { return false; }
        
//    virtual
//    bool IsGainFaderEnabled() const;
    
    void doOnStartProcessing(double sample_rate, SampleCount block_size) override {}
    void doProcess(ProcessInfo &pi) override {}
    void doOnStopProcessing() override {}
    
    void doSetTransportInfoWithPlaybackPosition(TransportInfo const &ti) override {}
    void doSetTransportInfoWithoutPlaybackPosition(TransportInfo const &ti) override {}
    
    std::unique_ptr<schema::Processor> ToSchemaImpl() const override {
        return nullptr;
    }
    
    String GetName() const override { return L"GraphProcessor"; };

public:
    struct Impl;

private:
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
