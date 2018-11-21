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

ClassInfo::CID Vst3Plugin::GetComponentID() const
{
    return pimpl_->GetComponentID();
}

String Vst3Plugin::GetEffectName() const
{
	return pimpl_->GetEffectName();
}

size_t Vst3Plugin::GetNumInputs() const
{
    return pimpl_->GetBusesInfo(Vst::BusDirections::kInput).GetNumActiveChannels();
}

size_t Vst3Plugin::GetNumOutputs() const
{
	return pimpl_->GetBusesInfo(Vst::BusDirections::kInput).GetNumActiveChannels();
}

UInt32  Vst3Plugin::GetNumParams() const
{
    return pimpl_->GetParameterInfoList().size();
}

Vst3Plugin::ParameterInfo const & Vst3Plugin::GetParameterInfoByIndex(UInt32 index) const
{
    return pimpl_->GetParameterInfoList().GetItemByIndex(index);
}
Vst3Plugin::ParameterInfo const & Vst3Plugin::GetParameterInfoByID(ParamID id) const
{
    return pimpl_->GetParameterInfoList().GetItemByID(id);
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

UInt32  Vst3Plugin::GetNumBuses(BusDirection dir) const
{
    return pimpl_->GetBusesInfo(dir).GetNumBuses();
}

Vst3Plugin::BusInfo const & Vst3Plugin::GetBusInfoByIndex(BusDirection dir, UInt32 index) const
{
    return pimpl_->GetBusesInfo(dir).GetBusInfo(index);
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

bool Vst3Plugin::IsBusActive(BusDirection dir, UInt32 index) const
{
    return pimpl_->GetBusesInfo(dir).GetBusInfo(index).is_active_;
}

void Vst3Plugin::SetBusActive(BusDirection dir, UInt32 index, bool state)
{
    pimpl_->GetBusesInfo(dir).SetActive(index, state);
}

Vst3Plugin::SpeakerArrangement Vst3Plugin::GetSpeakerArrangementForBus(BusDirection dir, UInt32 index) const
{
    return pimpl_->GetBusesInfo(dir).GetBusInfo(index).speaker_;
}

bool Vst3Plugin::SetSpeakerArrangement(BusDirection dir, UInt32 index, SpeakerArrangement arr)
{
    return pimpl_->GetBusesInfo(dir).SetSpeakerArrangement(index, arr);
}

void Vst3Plugin::Resume()
{
	pimpl_->Resume();
}

void Vst3Plugin::Suspend()
{
    CloseEditor();
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

std::unique_ptr<Vst3Plugin>
	CreatePlugin(IPluginFactory *factory,
                 ClassInfo const &info,
                 std::function<void(Vst3Plugin const *p)> on_destruction)
{
    auto host_context = std::make_unique<Vst3Plugin::HostContext>(L"Vst3HostDemo");
    auto impl = std::make_unique<Vst3Plugin::Impl>(factory, info, host_context->unknownCast());
	auto plugin = std::make_unique<Vst3Plugin>(std::move(impl), std::move(host_context), on_destruction);
    
    return plugin;
}

NS_HWM_END
