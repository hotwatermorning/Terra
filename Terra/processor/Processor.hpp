#pragma once

#include "./ProcessInfo.hpp"
#include <project.pb.h>
#include <plugin_desc.pb.h>

#include "../misc/LockFactory.hpp"

NS_HWM_BEGIN

enum class BusDirection { kInputSide, kOutputSide };

class Processor
{
protected:
    Processor()
    {}
    
public:
    virtual
    ~Processor() {}
    
    virtual String GetName() const = 0;
    
    virtual
    void OnStartProcessing(double sample_rate, SampleCount block_size)
    {}
    
    virtual
    void Process(ProcessInfo &pi) = 0;
    
    virtual
    void OnStopProcessing()
    {}
    
    virtual
    SampleCount GetLatencySample() const { return 0; }
    
    //! オーディオ入出力チャンネル数
    virtual
    UInt32 GetAudioChannelCount(BusDirection dir) const { return 0; }
    
    //! Midi入出力チャンネル数
    /*! ここでいうチャンネルは、Midiメッセージのチャンネルではなく、
     *  VST3のEventBusのインデックスを表す。
     */
    virtual
    UInt32 GetMidiChannelCount(BusDirection dir) const { return 0; }
    
    virtual
    bool HasEditor() const { return false; }
    
    std::unique_ptr<schema::Processor> ToSchema() const;
    
    static
    std::unique_ptr<Processor> FromSchema(schema::Processor const &schema);
    
private:
    virtual
    std::unique_ptr<schema::Processor> ToSchemaImpl() const = 0;
};

NS_HWM_END

//===================================================================

#include "../plugin/vst3/Vst3Plugin.hpp"

NS_HWM_BEGIN

class PluginAudioProcessor
:   public Processor
{
protected:
    PluginAudioProcessor(schema::PluginDescription const &desc)
    :   desc_(desc)
    {}
    
public:
    virtual
    ~PluginAudioProcessor()
    {}
    
    virtual
    bool IsLoaded() const = 0;
    
    struct LoadResult {
        std::string error_msg_;
        //! return true if the plugin was loaded successfully.
        explicit operator bool() const { return error_msg_.empty(); }
    };
    
    //! Do nothing and return a successful LoadResult if `IsLoaded() == true`.
    LoadResult Load();
    
    schema::PluginDescription const & GetDescription() const { return desc_; }
    
private:
    schema::PluginDescription desc_;
    
    virtual
    LoadResult doLoad() = 0;
};

class Vst3AudioProcessor
:   public PluginAudioProcessor
{
public:
    // lazy initialization
    Vst3AudioProcessor(schema::Processor const &vst3_data);
    
    Vst3AudioProcessor(schema::PluginDescription const &desc,
                       std::shared_ptr<Vst3Plugin> plugin);

    ~Vst3AudioProcessor();
    
    String GetName() const override;
    
    bool IsLoaded() const override;
    
    //! Do nothing and return a successful LoadResult if `IsLoaded() == true`.
    LoadResult doLoad() override;

    void OnStartProcessing(double sample_rate, SampleCount block_size) override;
    void Process(ProcessInfo &pi) override;
    void OnStopProcessing() override;
    SampleCount GetLatencySample() const override;
    UInt32 GetAudioChannelCount(BusDirection dir) const override;
    UInt32 GetMidiChannelCount(BusDirection dir) const override;
    bool HasEditor() const override;
    void CheckHavingEditor();
    
    std::unique_ptr<schema::Processor> ToSchemaImpl() const override;
    
    static
    std::unique_ptr<Vst3AudioProcessor> FromSchemaImpl(schema::Processor const &schema);
    
    schema::Processor schema_;
    std::shared_ptr<Vst3Plugin> plugin_;
    LockFactory process_lock_;
    
    struct ProcessSetting {
        double sample_rate_;
        SampleCount block_size_;
    };
    
private:
    std::optional<ProcessSetting> process_setting_;
    // apply saved data to the plugin if it has been resumed().
    void LoadDataImpl();
};

NS_HWM_END
