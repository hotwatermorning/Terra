#include "Vst3Plugin.hpp"
#include "Vst3PluginImpl.hpp"
#include "Vst3HostContext.hpp"

#include <cassert>
#include <memory>

NS_HWM_BEGIN

using namespace Steinberg;

Vst3Plugin::Vst3Plugin(std::unique_ptr<Impl> pimpl,
                       std::unique_ptr<HostContext> host_context,
                       std::function<void(Vst3Plugin const *p)> on_destruction)
:   pimpl_(std::move(pimpl))
,   host_context_(std::move(host_context))
{
    host_context_->SetVst3Plugin(this);
    on_destruction_ = on_destruction;
}

Vst3Plugin::~Vst3Plugin()
{
    assert(IsEditorOpened() == false);
    
	pimpl_.reset();
    host_context_.reset();
    on_destruction_(this);
}

FactoryInfo const & Vst3Plugin::GetFactoryInfo() const
{
    return pimpl_->GetFactoryInfo();
}

ClassInfo const & Vst3Plugin::GetComponentInfo() const
{
    return pimpl_->GetComponentInfo();
}

String Vst3Plugin::GetEffectName() const
{
	return pimpl_->GetEffectName();
}

size_t Vst3Plugin::GetNumInputs() const
{
    return pimpl_->GetAudioBusesInfo(Vst::BusDirections::kInput).GetNumActiveChannels();
}

size_t Vst3Plugin::GetNumOutputs() const
{
	return pimpl_->GetAudioBusesInfo(Vst::BusDirections::kOutput).GetNumActiveChannels();
}

UInt32  Vst3Plugin::GetNumParams() const
{
    return pimpl_->GetParameterInfoList().size();
}

Vst3Plugin::ParameterInfo const & Vst3Plugin::GetParameterInfoByIndex(UInt32 index) const
{
    return pimpl_->GetParameterInfoList().GetItemByIndex(index);
}
std::optional<Vst3Plugin::ParameterInfo>
    Vst3Plugin::FindParameterInfoByID(ParamID id) const
{
    return pimpl_->GetParameterInfoList().FindItemByID(id);
}

UInt32  Vst3Plugin::GetNumUnitInfo() const
{
    return pimpl_->GetUnitInfoList().size();
}

Vst3Plugin::UnitInfo const & Vst3Plugin::GetUnitInfoByIndex(UInt32 index) const
{
    return pimpl_->GetUnitInfoList().GetItemByIndex(index);
}

Vst3Plugin::UnitInfo const & Vst3Plugin::GetUnitInfoByID(UnitID id) const
{
    return pimpl_->GetUnitInfoList().GetItemByID(id);
}

UInt32  Vst3Plugin::GetNumBuses(MediaTypes media, BusDirections dir) const
{
    if(media == MediaTypes::kAudio) {
        return pimpl_->GetAudioBusesInfo(dir).GetNumBuses();
    } else {
        return pimpl_->GetMidiBusesInfo(dir).GetNumBuses();
    }
}

Vst3Plugin::BusInfo const & Vst3Plugin::GetBusInfoByIndex(MediaTypes media, BusDirections dir, UInt32 index) const
{
    if(media == MediaTypes::kAudio) {
        return pimpl_->GetAudioBusesInfo(dir).GetBusInfo(index);
    } else {
        return pimpl_->GetMidiBusesInfo(dir).GetBusInfo(index);
    }
}

Vst3Plugin::ParamValue Vst3Plugin::GetParameterValueByIndex(UInt32 index) const
{
    return pimpl_->GetParameterValueByIndex(index);
}

Vst3Plugin::ParamValue Vst3Plugin::GetParameterValueByID(ParamID id) const
{
    return pimpl_->GetParameterValueByID(id);
}

String Vst3Plugin::ValueToStringByIndex(UInt32 index, ParamValue value)
{
    return pimpl_->ValueToStringByIndex(index, value);
}

Vst::ParamValue Vst3Plugin::StringToValueTByIndex(UInt32 index, String string)
{
    return pimpl_->StringToValueTByIndex(index, string);
}

String Vst3Plugin::ValueToStringByID(ParamID id, ParamValue value)
{
    return pimpl_->ValueToStringByID(id, value);
}

Vst::ParamValue Vst3Plugin::StringToValueByID(ParamID id, String string)
{
    return pimpl_->StringToValueByID(id, string);
}

bool Vst3Plugin::IsBusActive(MediaTypes media, BusDirections dir, UInt32 index) const
{
    return GetBusInfoByIndex(media, dir, index).is_active_;
}

void Vst3Plugin::SetBusActive(MediaTypes media, BusDirections dir, UInt32 index, bool state)
{
    if(media == MediaTypes::kAudio) {
        pimpl_->GetAudioBusesInfo(dir).SetActive(index, state);
    } else {
        pimpl_->GetMidiBusesInfo(dir).SetActive(index, state);
    }
}

UInt32 Vst3Plugin::GetNumActiveBuses(MediaTypes media, BusDirections dir) const
{
    assert(media == MediaTypes::kEvent);
    return pimpl_->GetMidiBusesInfo(dir).GetNumActiveBuses();
}

Vst3Plugin::SpeakerArrangement Vst3Plugin::GetSpeakerArrangementForBus(BusDirections dir, UInt32 index) const
{
    return pimpl_->GetAudioBusesInfo(dir).GetBusInfo(index).speaker_;
}

bool Vst3Plugin::SetSpeakerArrangement(BusDirections dir, UInt32 index, SpeakerArrangement arr)
{
    return pimpl_->GetAudioBusesInfo(dir).SetSpeakerArrangement(index, arr);
}

void Vst3Plugin::Resume()
{
	pimpl_->Resume();
}

void Vst3Plugin::Suspend()
{
    //CloseEditor();
	pimpl_->Suspend();
}

bool Vst3Plugin::IsResumed() const
{
	return pimpl_->IsResumed();
}

void Vst3Plugin::SetBlockSize(int block_size)
{
	assert(!IsResumed());
	pimpl_->SetBlockSize(block_size);
}

void Vst3Plugin::SetSamplingRate(int sampling_rate)
{
	assert(!IsResumed());
	pimpl_->SetSamplingRate(sampling_rate);
}

bool Vst3Plugin::HasEditor() const
{
	return pimpl_->HasEditor();
}

void Vst3Plugin::CheckHavingEditor()
{
    return pimpl_->CheckHavingEditor();
}

bool Vst3Plugin::OpenEditor(WindowHandle parent, PlugFrameListener *listener)
{
    //! not support multiple plug view yet.
    CloseEditor();
    
    assert(listener);
    assert(host_context_->plug_frame_listener_ == nullptr);
    
    host_context_->plug_frame_listener_ = listener;
    return pimpl_->OpenEditor(parent, host_context_.get());
}

void Vst3Plugin::CloseEditor()
{
	pimpl_->CloseEditor();
    host_context_->plug_frame_listener_ = nullptr;
}

bool Vst3Plugin::IsEditorOpened() const
{
	return pimpl_->IsEditorOpened();
}

ViewRect Vst3Plugin::GetPreferredRect() const
{
	return pimpl_->GetPreferredRect();
}

UInt32 Vst3Plugin::GetProgramIndex(UnitID id) const
{
	return pimpl_->GetProgramIndex(id);
}

void Vst3Plugin::SetProgramIndex(UInt32 index, UnitID id)
{
	pimpl_->SetProgramIndex(index, id);
}

void Vst3Plugin::EnqueueParameterChange(Vst::ParamID id, Vst::ParamValue value)
{
	pimpl_->PushBackParameterChange(id, value);
}

void Vst3Plugin::RestartComponent(Steinberg::int32 flags)
{
	pimpl_->RestartComponent(flags);
}

void Vst3Plugin::Process(ProcessInfo &pi)
{
    pimpl_->Process(pi);
}

std::optional<Vst3Plugin::DumpData> Vst3Plugin::SaveData() const
{
    return pimpl_->SaveData();
}

void Vst3Plugin::LoadData(DumpData const &dump)
{
    pimpl_->LoadData(dump);
}

std::unique_ptr<Vst3Plugin>
	CreatePlugin(IPluginFactory *factory,
                 FactoryInfo const &factory_info,
                 ClassInfo const &class_info,
                 std::function<void(Vst3Plugin const *p)> on_destruction)
{
    auto host_context = std::make_unique<Vst3Plugin::HostContext>(kAppName);
    auto impl = std::make_unique<Vst3Plugin::Impl>(factory, factory_info, class_info, host_context->unknownCast());
	auto plugin = std::make_unique<Vst3Plugin>(std::move(impl), std::move(host_context), on_destruction);
    
    return plugin;
}

NS_HWM_END
