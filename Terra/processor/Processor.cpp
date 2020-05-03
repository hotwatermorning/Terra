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

//class Processor::TransportStateListener
//:   public Transport::ITransportStateListener
//{
//    TransportStateListener(Processor *p)
//    :   owner_(p)
//    {}
//
//    void OnChanged(TransportInfo const &old_state, TransportInfo const &new_state);
//};

Processor::Processor()
{}

Processor::~Processor()
{}

void Processor::ResetTransporter(IMusicalTimeService const *mts)
{
    tp_ = std::make_unique<Transporter>(mts);
}

Transporter * Processor::GetTransporter()
{
    return tp_.get();
}

Transporter const * Processor::GetTransporter() const
{
    return tp_.get();
}

TransportInfo Processor::GetTransportInfo() const
{
    return tp_->GetCurrentState();
}

void Processor::SetTransportInfoWithPlaybackPosition(TransportInfo const &ti)
{
    tp_->SetCurrentStateWithPlaybackPosition(ti);
}

void Processor::SetTransportInfoWithoutPlaybackPosition(TransportInfo const &ti)
{
    tp_->SetCurrentStateWithoutPlaybackPosition(ti);
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

void Processor::OnStartProcessing(double sample_rate, SampleCount block_size)
{
    volume_.update_transition(INT_MAX);
    doOnStartProcessing(sample_rate, block_size);
}

void Processor::Process(ProcessInfo &pi)
{
    doProcess(pi);
    
    if(IsGainFaderEnabled() == false) {
        volume_.update_transition(pi.time_info_->play_.duration_.sample_);
        return;
    }
    
    auto gain = volume_.get_current_linear_gain();
    if(gain == 1) {
        //do nothing.
    } else if(gain == 0) {
        auto &buf = pi.output_audio_buffer_;
        for(UInt32 ch = buf.channel_from(); ch < buf.channels(); ++ch) {
            std::fill_n(buf.data()[ch], buf.samples(), 0);
        }
    } else {
        auto &buf = pi.output_audio_buffer_;
        for(UInt32 ch = buf.channel_from(); ch < buf.channels(); ++ch) {
            std::for_each_n(buf.data()[ch], pi.time_info_->play_.duration_.sample_, [gain](auto &x) { x *= gain; });
        }
    }
    
    volume_.update_transition(pi.time_info_->play_.duration_.sample_);
}

void Processor::OnStopProcessing()
{
    doOnStopProcessing();
}

bool Processor::IsGainFaderEnabled() const
{
    return GetAudioChannelCount(BusDirection::kOutputSide) > 0;
}

double Processor::GetVolumeLevelMin() const
{
    return volume_.get_min_db();
}

double Processor::GetVolumeLevelMax() const
{
    return volume_.get_max_db();
}

//! Set volume in the range [0.0 .. 1.0].
void Processor::SetVolumeLevel(double dB)
{
    volume_.set_target_db(dB);
}

//! Get volume in the range [0.0 .. 1.0].
double Processor::GetVolumeLevel()
{
    return volume_.get_target_db();
}

void Processor::SetVolumeLevelImmediately(double dB)
{
    volume_.set_target_db_immediately(dB);
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
    
    auto app = App::GetInstance();
    auto p = app->CreateVst3Plugin(GetDescription());
    
    assert(schema_.has_vst3_data());
    
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
 
    LoadDataImpl();
    return LoadResult{};
}

void Vst3AudioProcessor::LoadDataImpl()
{
    auto p = std::atomic_load(&plugin_);
    if(!p) { return; }
    
    // for a newly created plugin processor.
    if(schema_.has_vst3_data() == false) {
        return;
    }
    
    if(!p->IsResumed()) {
        return;
    }

    auto &vd = schema_.vst3_data();
    if(vd.has_dump()) {
        Vst3Plugin::DumpData dd;
        auto const &proc_data = vd.dump().processor_data();
        auto const &edit_data = vd.dump().edit_controller_data();
        
        dd.processor_data_.assign(proc_data.begin(), proc_data.end());
        dd.edit_controller_data_.assign(edit_data.begin(), edit_data.end());
        p->LoadData(dd);
    } else if(vd.params().size() > 0) {
        // TODO: set parameters to both processor and edit controller.
    }
    
    auto mvd = schema_.mutable_vst3_data();
    mvd->clear_dump();
    mvd->clear_params();
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

void Vst3AudioProcessor::doOnStartProcessing(double sample_rate, SampleCount block_size)
{
    auto lock = process_lock_.make_lock();
    
    ProcessSetting ps;
    ps.sample_rate_ = sample_rate;
    ps.block_size_ = block_size;

    if(plugin_) {
        assert(plugin_->IsResumed() == false);
        apply_process_setting(ps, plugin_.get());
        LoadDataImpl();
    } else {
        process_setting_ = ps;
    }
}

void Vst3AudioProcessor::doProcess(ProcessInfo &pi)
{
    auto lock = process_lock_.make_lock();
    if(plugin_) {
        plugin_->Process(pi);
    }
}

void Vst3AudioProcessor::doOnStopProcessing()
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

void Vst3AudioProcessor::CheckHavingEditor()
{
    auto p = std::atomic_load(&plugin_);
    
    if(p) {
        plugin_->CheckHavingEditor();
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
        
        auto dump = plugin_->SaveData();
        if(dump) {
            auto schema_dump = vst3->mutable_dump();
            schema_dump->set_processor_data(dump->processor_data_.data(),
                                            dump->processor_data_.size());
            schema_dump->set_edit_controller_data(dump->edit_controller_data_.data(),
                                                  dump->edit_controller_data_.size());
        } else {
            auto const num_params = plugin_->GetNumParams();
            for(int i = 0; i < num_params; ++i) {
                auto const &param_info = plugin_->GetParameterInfoByIndex(i);
                auto sparam = vst3->add_params();
                sparam->set_id(param_info.id_);
                sparam->set_value(plugin_->GetParameterValueByIndex(i));
            }
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
