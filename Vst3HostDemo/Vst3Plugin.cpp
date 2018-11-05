#include "./Vst3Plugin.hpp"
#include "./Vst3Plugin/Vst3PluginImpl.hpp"

#include <cassert>
#include <memory>

NS_HWM_BEGIN

using namespace Steinberg;

Vst3Plugin::ParameterAccessor::ParameterAccessor(Vst3Plugin *owner)
	:	owner_(owner)
{}

size_t Vst3Plugin::ParameterAccessor::size() const 
{
	return owner_->pimpl_->parameters_.size();
}

Vst3Plugin::ParameterAccessor::value_t
		Vst3Plugin::ParameterAccessor::get_by_index(size_t index) const
{
	auto controller = owner_->pimpl_->GetEditController();
	return
		controller->getParamNormalized(owner_->pimpl_->parameters_.GetInfoByIndex(index).id);
}

void	Vst3Plugin::ParameterAccessor::set_by_index(size_t index, value_t value)
{
	auto controller = owner_->pimpl_->GetEditController();
	controller->setParamNormalized(owner_->pimpl_->parameters_.GetInfoByIndex(index).id, value);
}

Vst3Plugin::ParameterAccessor::value_t
		Vst3Plugin::ParameterAccessor::get_by_id(Vst::ParamID id) const
{
	auto controller = owner_->pimpl_->GetEditController();
	return
		controller->getParamNormalized(id);
}

void	Vst3Plugin::ParameterAccessor::set_by_id(Vst::ParamID id, value_t value)
{
	auto controller = owner_->pimpl_->GetEditController();
	controller->setParamNormalized(id, value);
}

Vst::ParameterInfo
		Vst3Plugin::ParameterAccessor::info(size_t index) const
{
	auto controller = owner_->pimpl_->GetEditController();

	Vst::ParameterInfo info = {};
	controller->getParameterInfo(index, info);
	return info;
}

Vst3Plugin::Vst3Plugin(std::unique_ptr<Impl> pimpl,
                       std::function<void(Vst3Plugin const *p)> on_destruction)
{
	pimpl_ = std::move(pimpl);
	parameters_ = std::make_unique<ParameterAccessor>(this);
    on_destruction_ = on_destruction;
}

Vst3Plugin::~Vst3Plugin()
{
	pimpl_.reset();
    on_destruction_(this);
}

Vst3Plugin::ParameterAccessor &
	Vst3Plugin::GetParams()
{
		return *parameters_;
}

Vst3Plugin::ParameterAccessor const &
	Vst3Plugin::GetParams() const
{
		return *parameters_;
}

String Vst3Plugin::GetEffectName() const
{
	return pimpl_->GetEffectName();
}

size_t Vst3Plugin::GetNumOutputs() const
{
	return pimpl_->GetNumOutputs();
}

void Vst3Plugin::Resume()
{
	pimpl_->Resume();
}

void Vst3Plugin::Suspend()
{
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

//bool Vst3Plugin::OpenEditor(HWND parent, Steinberg::IPlugFrame *frame)
//{
//    return pimpl_->OpenEditor(parent, frame);
//}

void Vst3Plugin::CloseEditor()
{
	pimpl_->CloseEditor();
}

bool Vst3Plugin::IsEditorOpened() const
{
	return pimpl_->IsEditorOpened();
}

ViewRect Vst3Plugin::GetPreferredRect() const
{
	return pimpl_->GetPreferredRect();
}

void Vst3Plugin::AddNoteOn(int note_number)
{
	pimpl_->AddNoteOn(note_number);
}

void Vst3Plugin::AddNoteOff(int note_number)
{
	pimpl_->AddNoteOff(note_number);
}

size_t			Vst3Plugin::GetProgramCount() const
{
	return pimpl_->GetProgramCount();
}

String	Vst3Plugin::GetProgramName(size_t index) const
{
	return pimpl_->GetProgramName(index);
}

size_t			Vst3Plugin::GetProgramIndex() const
{
	return pimpl_->GetProgramIndex();
}

void			Vst3Plugin::SetProgramIndex(size_t index)
{
	pimpl_->SetProgramIndex(index);
}

void Vst3Plugin::EnqueueParameterChange(Vst::ParamID id, Vst::ParamValue value)
{
	pimpl_->EnqueueParameterChange(id, value);
}

void Vst3Plugin::RestartComponent(Steinberg::int32 flags)
{
	pimpl_->RestartComponent(flags);
}

float ** Vst3Plugin::ProcessAudio(size_t frame_pos, size_t duration)
{
	return pimpl_->ProcessAudio(frame_pos, duration);
}

std::unique_ptr<Vst3Plugin>
	CreatePlugin(IPluginFactory *factory,
                 ClassInfo const &info,
                 Vst3PluginFactory::host_context_type host_context,
                 std::function<void(Vst3Plugin const *p)> on_destruction)
{
	auto impl = std::make_unique<Vst3Plugin::Impl>(factory,
                                                   info,
                                                   std::move(host_context));
	return std::make_unique<Vst3Plugin>(std::move(impl),
                                        on_destruction);
}

NS_HWM_END
