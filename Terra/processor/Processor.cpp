#pragma once

#include "./Processor.hpp"
#include "../project/GraphProcessor.hpp"
#include "../misc/StrCnv.hpp"
#include "../App.hpp"

NS_HWM_BEGIN

static
Vst3Plugin::BusDirections ToVst3BusDirection(BusDirection dir)
{
    return
    dir == BusDirection::kInputSide
    ? Vst3Plugin::BusDirections::kInput
    : Vst3Plugin::BusDirections::kOutput;
}

std::unique_ptr<schema::Processor> Processor::ToSchema() const
{
    return ToSchemaImpl();
}

std::unique_ptr<Processor> Processor::FromSchema(schema::Processor const &schema)
{
    if(schema.has_vst3_data()) {
        return Vst3AudioProcessor::FromSchemaImpl(schema);
    }
    
    assert(false);
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////


PluginAudioProcessor::LoadResult PluginAudioProcessor::Load()
{
    if(IsLoaded()) { return LoadResult{}; }
    
    return doLoad();
}

////////////////////////////////////////////////////////////////////////////////////////////

schema::PluginDescription const & GetSavedDescription(schema::Processor const &proc)
{
    assert(proc.has_vst3_data() && proc.vst3_data().has_desc());
    return proc.vst3_data().desc();
}

// lazy initialization
Vst3AudioProcessor::Vst3AudioProcessor(schema::Processor const &schema)
:   PluginAudioProcessor(GetSavedDescription(schema))
,   schema_(schema)
{}

Vst3AudioProcessor::Vst3AudioProcessor(schema::PluginDescription const &desc,
                                       std::shared_ptr<Vst3Plugin> plugin)
:   hwm::PluginAudioProcessor(desc)
,   plugin_(plugin)
{}

Vst3AudioProcessor::~Vst3AudioProcessor()
{}

bool Vst3AudioProcessor::IsLoaded() const
{
    auto lock = process_lock_.make_lock();
    return plugin_ != nullptr;
}

auto apply_process_setting = [](Vst3AudioProcessor::ProcessSetting const &setting,
                                Vst3Plugin *plugin)
{
    if(plugin->IsResumed()) { plugin->Suspend(); }
    
    plugin->SetSamplingRate(setting.sample_rate_);
    plugin->SetBlockSize(setting.block_size_);
    plugin->Resume();
};

PluginAudioProcessor::LoadResult Vst3AudioProcessor::doLoad()
{
    {
        auto lock = process_lock_.make_lock();
        if(plugin_) { return LoadResult{}; }
    }
    
    auto app = MyApp::GetInstance();
    auto p = app->CreateVst3Plugin(GetDescription());
    
    for( ; ; ) {
        std::optional<ProcessSetting> copied_process_setting;
        
        auto lock = process_lock_.make_lock();
        copied_process_setting = process_setting_;
        process_setting_ = std::nullopt;
        
        if(!copied_process_setting) {
            plugin_ = std::move(p);
            break;
        }
        
        lock.unlock();
        apply_process_setting(*copied_process_setting, p.get());
    }
    
    return LoadResult{};
}

String Vst3AudioProcessor::GetName() const
{
    auto p = std::atomic_load(&plugin_);
    if(p) {
        return p->GetEffectName();
    } else {
        return to_wstr(GetDescription().name());
    }
}

void Vst3AudioProcessor::OnStartProcessing(double sample_rate, SampleCount block_size)
{
    auto lock = process_lock_.make_lock();

    if(plugin_) {
        assert(plugin_->IsResumed() == false);
        plugin_->SetSamplingRate(sample_rate);
        plugin_->SetBlockSize(block_size);
        plugin_->Resume();
    } else {
        ProcessSetting ps;
        ps.sample_rate_ = sample_rate;
        ps.block_size_ = block_size;
        process_setting_ = ps;
    }
}

void Vst3AudioProcessor::Process(ProcessInfo &pi)
{
    auto lock = process_lock_.make_lock();
    if(plugin_) {
        plugin_->Process(pi);
    }
}

void Vst3AudioProcessor::OnStopProcessing()
{
    auto lock = process_lock_.make_lock();

    if(plugin_) {
        plugin_->Suspend();
    } else {
        process_setting_ = std::nullopt;
    }
}

SampleCount Vst3AudioProcessor::GetLatencySample() const
{
    return 0;
}

UInt32 Vst3AudioProcessor::GetAudioChannelCount(BusDirection dir) const
{
    auto p = std::atomic_load(&plugin_);
    if(p) {
        if(dir == BusDirection::kInputSide) { return p->GetNumInputs(); }
        else                                { return p->GetNumOutputs(); }
    } else {
        assert(schema_.has_vst3_data());
        auto &buses
        = (dir == BusDirection::kInputSide)
        ?   schema_.vst3_data().audio_input_buses()
        :   schema_.vst3_data().audio_output_buses();
        
        int sum = 0;
        for(auto const &bus: buses) {
            sum += bus.num_channels();
        }
        
        return sum;
    }
}

UInt32 Vst3AudioProcessor::GetMidiChannelCount(BusDirection dir) const
{
    auto p = std::atomic_load(&plugin_);
    if(p) {
        auto const media = Vst3Plugin::MediaTypes::kEvent;
        return p->GetNumActiveBuses(media, ToVst3BusDirection(dir));
    } else {
        assert(schema_.has_vst3_data());
        auto &buses
        = (dir == BusDirection::kInputSide)
        ?   schema_.vst3_data().event_input_buses()
        :   schema_.vst3_data().event_output_buses();
        
        return buses.size();
    }
}

bool Vst3AudioProcessor::HasEditor() const
{
    auto p = std::atomic_load(&plugin_);

    if(p) {
        return plugin_->HasEditor();
    } else {
        return false;
    }
}

void FillBusSchema(schema::Processor_Vst3_Bus &schema, Vst3Plugin::BusInfo const &bus)
{
    schema.set_name(to_utf8(bus.name_));
    schema.set_bus_type(bus.bus_type_);
    schema.set_num_channels(bus.channel_count_);
    if(bus.media_type_ == Vst3Plugin::MediaTypes::kAudio) {
        schema.set_speaker(bus.speaker_);
    }
}

std::unique_ptr<schema::Processor> Vst3AudioProcessor::ToSchemaImpl() const
{
    auto p = std::atomic_load(&plugin_);

    auto schema = std::make_unique<schema::Processor>();
    auto vst3 = schema->mutable_vst3_data();
    
    if(p) {
        auto add_buses = [&](auto media, auto dir, auto f) {
            auto num = plugin_->GetNumBuses(media, dir);
            for(int i = 0; i < num; ++i) {
                auto info = plugin_->GetBusInfoByIndex(media, dir, i);
                if(info.is_active_ == false) { return; }
                f(info);
            }
        };
        using MT = Vst3Plugin::MediaTypes;
        using BD = Vst3Plugin::BusDirections;
        
        add_buses(MT::kAudio, BD::kInput,
                  [&](auto const &bus_info) {
                      FillBusSchema(*vst3->add_audio_input_buses(), bus_info);
                  });
        
        add_buses(MT::kAudio, BD::kOutput,
                  [&](auto const &bus_info) {
                      FillBusSchema(*vst3->add_audio_output_buses(), bus_info);
                  });
        
        add_buses(MT::kEvent, BD::kInput,
                  [&](auto const &bus_info) {
                      FillBusSchema(*vst3->add_event_input_buses(), bus_info);
                  });
        
        add_buses(MT::kEvent, BD::kOutput,
                  [&](auto const &bus_info) {
                      FillBusSchema(*vst3->add_event_output_buses(), bus_info);
                  });
        
        auto const num_params = plugin_->GetNumParams();
        for(int i = 0; i < num_params; ++i) {
            auto const &param_info = plugin_->GetParameterInfoByIndex(i);
            auto sparam = vst3->add_params();
            sparam->set_id(param_info.id_);
            sparam->set_value(plugin_->GetParameterValueByIndex(i));
        }
        
        *vst3->mutable_desc() = GetDescription();
    } else {
        *schema = schema_;
    }
    
    return schema;
}

std::unique_ptr<Vst3AudioProcessor> Vst3AudioProcessor::FromSchemaImpl(schema::Processor const &schema)
{
    assert(schema.has_vst3_data());
    
    return std::make_unique<Vst3AudioProcessor>(schema);
}

NS_HWM_END
