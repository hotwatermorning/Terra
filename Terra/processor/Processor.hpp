#pragma once

#include "./ProcessInfo.hpp"

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
};

NS_HWM_END

//===================================================================

#include "../plugin/vst3/Vst3Plugin.hpp"

NS_HWM_BEGIN

class Vst3AudioProcessor
:   public Processor
{
public:
    Vst3AudioProcessor(std::shared_ptr<Vst3Plugin> plugin)
    :   plugin_(plugin)
    {}
    
    String GetName() const override { return plugin_->GetEffectName(); }

    void OnStartProcessing(double sample_rate, SampleCount block_size) override
    {
        assert(plugin_->IsResumed() == false);
        plugin_->SetSamplingRate(sample_rate);
        plugin_->SetBlockSize(block_size);
        plugin_->Resume();
    }
    
    void Process(ProcessInfo &pi)  override
    {
        plugin_->Process(pi);
    }
    
    void OnStopProcessing() override
    {
        plugin_->Suspend();
    }
    
    SampleCount GetLatencySample() const  override
    {
        return 0;
    }
    
    UInt32 GetAudioChannelCount(BusDirection dir) const override
    {
        if(dir == BusDirection::kInputSide) { return plugin_->GetNumInputs(); }
        else                                { return plugin_->GetNumOutputs(); }
    }
    
    static
    Vst3Plugin::BusDirections ToVst3BusDirection(BusDirection dir)
    {
        return
        dir == BusDirection::kInputSide
        ? Vst3Plugin::BusDirections::kInput
        : Vst3Plugin::BusDirections::kOutput;
    }
    
    UInt32 GetMidiChannelCount(BusDirection dir) const override
    {
        auto const media = Vst3Plugin::MediaTypes::kEvent;
        return plugin_->GetNumActiveBuses(media, ToVst3BusDirection(dir));
    }
    
    bool HasEditor() const override { return plugin_->HasEditor(); }
    
    std::shared_ptr<Vst3Plugin> plugin_;
};

NS_HWM_END
