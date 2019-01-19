#pragma once
#include "../plugin/vst3/Vst3Plugin.hpp"
#include "../misc/ThreadSafeRingBuffer.hpp"
#include "../misc/LockFactory.hpp"
#include "../transport/TransportInfo.hpp"
#include "../processor/Processor.hpp"
#include "./Sequence.hpp"

NS_HWM_BEGIN

//! unlike juce::AudioProcessorGraph,
//! This class does not inherit Processor,
//! because currently this class is intended to only be used with the Project class,
//! and therefore no need to be so generic as much as juce::AudioGraphProcessor which is also juce::AudioProcessor.
class GraphProcessor
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
    
    struct Listener : public ListenerBase
    {
    protected:
        Listener() {}
    public:
        virtual void OnAfterNodeIsAdded(Node *node) {};
        virtual void OnBeforeNodeIsRemoved(Node *node) {};
    };
    
public:
    GraphProcessor();
    ~GraphProcessor();
    
    class AudioInput : public Processor {
    public:
        virtual void SetData(BufferRef<float const> buf) = 0;
    };
    
    class AudioOutput : public Processor {
    public:
        virtual BufferRef<float const> GetData() const = 0;
    };
    
    class MidiInput : public Processor {
    public:
        using BufferType = ArrayRef<ProcessInfo::MidiMessage const>;
        virtual void SetData(BufferType buf) = 0;
    };
    
    class MidiOutput : public Processor {
    public:
        using BufferType = ArrayRef<ProcessInfo::MidiMessage const>;
        virtual BufferType GetData() const = 0;
    };
    
public:
    //! @param
    AudioInput *    AddAudioInput(String name, UInt32 num_channels,
                                  std::function<void(AudioInput *, ProcessInfo const &)> callback);
    AudioOutput *   AddAudioOutput(String name, UInt32 num_channels,
                                   std::function<void(AudioOutput *, ProcessInfo const &)> callback);
    MidiInput *    AddMidiInput(String name, std::function<void(MidiInput *, ProcessInfo const &)> callback);
    MidiOutput *   AddMidiOutput(String name, std::function<void(MidiOutput *, ProcessInfo const &)> callback);
    
    void RemoveAudioInput(AudioInput const *);
    void RemoveAudioOutput(AudioOutput const *);
    void RemoveMidiInput(MidiInput const *);
    void RemoveMidiOutput(MidiOutput const *);
    
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
    void Process(TransportInfo const &ti);
    void StopProcessing();
    
    class Connection
    {
    protected:
        Connection(Node *upstream, Node *downstream)
        :   upstream_(upstream)
        ,   downstream_(downstream)
        {}
        
    public:
        virtual ~Connection() {}
        
        Node *upstream_;
        Node *downstream_;
    };
    
    class AudioConnection : public Connection
    {
    public:
        AudioConnection(Node *upstream, Node *downstream,
                        UInt32 upstream_channel_index, UInt32 downstream_channel_index,
                        UInt32 num_channels)
        :   Connection(upstream, downstream)
        ,   upstream_channel_index_(upstream_channel_index)
        ,   downstream_channel_index_(downstream_channel_index)
        ,   num_channels_(num_channels)
        {}
        
        UInt32 upstream_channel_index_;
        UInt32 downstream_channel_index_;
        UInt32 num_channels_; // 1 means mono
    };
    
    class MidiConnection : public Connection
    {
    public:
        MidiConnection(Node *upstream, Node *downstream,
                       UInt32 upstream_channel_index, UInt32 downstream_channel_index)
        :   Connection(upstream, downstream)
        ,   upstream_channel_index_(upstream_channel_index)
        ,   downstream_channel_index_(downstream_channel_index)
        {}
        
        UInt32 upstream_channel_index_;
        UInt32 downstream_channel_index_;
    };
    
    class Node
    {
    protected:
        Node(std::shared_ptr<Processor> processor)
        :   processor_(std::move(processor))
        {}
        
    public:
        virtual ~Node() {}
        
        std::shared_ptr<Processor> GetProcessor() const { return processor_; }
        
        std::vector<AudioConnectionPtr> GetAudioConnections(BusDirection dir) const;
        std::vector<MidiConnectionPtr> GetMidiConnections(BusDirection dir) const;
        
        std::vector<AudioConnectionPtr> GetAudioConnectionsTo(BusDirection dir, Node const *target) const;
        std::vector<MidiConnectionPtr> GetMidiConnectionsTo(BusDirection dir, Node const *target) const;
        
        bool HasAudioPathTo(Node const *downstream) const;
        bool HasMidiPathTo(Node const *downstream) const;
        bool HasPathTo(Node const *downstream) const;
        
    protected:
        std::shared_ptr<Processor> processor_;
        std::vector<AudioConnectionPtr> input_audio_connections_;
        std::vector<AudioConnectionPtr> output_audio_connections_;
        std::vector<MidiConnectionPtr> input_midi_connections_;
        std::vector<MidiConnectionPtr> output_midi_connections_;
    };
    
    //! don't call this function on the realtime thread.
    //! Nothing to do if the processor is added aleady.
    NodePtr AddNode(std::shared_ptr<Processor> processor);
        
    //! don't call this function on the realtime thread.
    //! Nothing to do if the processor is not added.
    std::shared_ptr<Processor> RemoveNode(Processor const *processor);
    
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
    bool Disconnect(std::shared_ptr<Connection> conn);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
