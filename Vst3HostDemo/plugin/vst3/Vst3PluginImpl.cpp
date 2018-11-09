#include "Vst3PluginImpl.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <vector>

#include "../../misc/StrCnv.hpp"
#include "../../misc/ScopeExit.hpp"
#include "Vst3Utils.hpp"
#include "Vst3Plugin.hpp"
#include "Vst3PluginFactory.hpp"

using namespace Steinberg;

NS_HWM_BEGIN

extern void* GetWindowRef(NSView *view);

Vst3Plugin::Impl::Impl(IPluginFactory *factory,
                       ClassInfo const &info,
                       FUnknown *host_context)
	:	edit_controller_is_created_new_(false)
	,	is_editor_opened_(false)
	,	is_processing_started_(false)
	,	is_resumed_(false)
	,	block_size_(2048)
	,	sampling_rate_(44100)
	,	has_editor_(false)
	,	current_program_index_(-1)
	,	program_change_parameter_(-1)
	,	status_(Status::kInvalid)
{
    assert(host_context);
    
	LoadPlugin(factory, info, std::move(host_context));

	size_t const sampling_rate = 44100;
	size_t const length = sampling_rate * 2;
	double const rad = 2 * 3.141592653589793;
	double const amp = 2 / 3.141592653589793 * 0.125;

	wave_data_.resize(length);
	double const head_freq = 440;
	double const last_freq = 220;
	double const freq_angle = (last_freq - head_freq) / length;

	double current_freq = head_freq;
	double pos = 0;

	for(size_t i = 0; i < length; ++i) {
		for(int k = 1; k <= 30; ++k) {
			wave_data_[i] += sin(rad * k * pos) / (double)(k);
		}
		wave_data_[i] *= amp;
			
		double const progress = current_freq / sampling_rate;
		pos += progress;
		current_freq += freq_angle;
	}
	wave_data_index_ = 0;
}

Vst3Plugin::Impl::~Impl()
{
	UnloadPlugin();
}

bool Vst3Plugin::Impl::HasEditController	() const { return edit_controller_.get() != nullptr; }
bool Vst3Plugin::Impl::HasEditController2	() const { return edit_controller2_.get() != nullptr; }

Vst::IComponent	*		Vst3Plugin::Impl::GetComponent		() { return component_.get(); }
Vst::IAudioProcessor *	Vst3Plugin::Impl::GetAudioProcessor	() { return audio_processor_.get(); }
Vst::IEditController *	Vst3Plugin::Impl::GetEditController	() { return edit_controller_.get(); }
Vst::IEditController2 *	Vst3Plugin::Impl::GetEditController2	() { return edit_controller2_.get(); }
Vst::IEditController *	Vst3Plugin::Impl::GetEditController	() const { return edit_controller_.get(); }
Vst::IEditController2 *	Vst3Plugin::Impl::GetEditController2	() const { return edit_controller2_.get(); }

String Vst3Plugin::Impl::GetEffectName() const
{
	return plugin_info_->name();
}

size_t Vst3Plugin::Impl::GetNumOutputs() const
{
	return output_buses_.GetTotalChannels();
}

bool Vst3Plugin::Impl::HasEditor() const
{
	assert(component_);
	assert(is_resumed_);
	return has_editor_.get();
}

bool Vst3Plugin::Impl::OpenEditor(WindowHandle parent, IPlugFrame *frame)
{
    assert(HasEditor());

    tresult res;
    
    if(frame) {
        plug_view_->setFrame(frame);
    }
    
#if defined(_MSC_VER)
    res = plug_view_->attached((void *)parent, kPlatformTypeHWND);
#else
    if(plug_view_->isPlatformTypeSupported(kPlatformTypeNSView) == kResultOk) {
        res = plug_view_->attached((void *)parent, kPlatformTypeNSView);
    } else if(plug_view_->isPlatformTypeSupported(kPlatformTypeHIView) == kResultOk) {
        res = plug_view_->attached(GetWindowRef(parent), kPlatformTypeHIView);
    } else {
        assert(false);
    }
#endif
    
    is_editor_opened_ = (res == kResultOk);
    return is_editor_opened_;
}

void Vst3Plugin::Impl::CloseEditor()
{
	if(is_editor_opened_) {
		plug_view_->removed();
		is_editor_opened_ = false;
	}
}

bool Vst3Plugin::Impl::IsEditorOpened() const
{
	return is_editor_opened_.get();
}

ViewRect Vst3Plugin::Impl::GetPreferredRect() const
{
	assert(IsEditorOpened());

	ViewRect rect;
	plug_view_->getSize(&rect);
	return rect;
}

void Vst3Plugin::Impl::Resume()
{
	assert(status_ == Status::kInitialized || status_ == Status::kSetupDone);

	tresult res;
	if(status_ != Status::kSetupDone) {
		Vst::ProcessSetup setup = {};
		setup.maxSamplesPerBlock = block_size_;
		setup.sampleRate = sampling_rate_;
		setup.symbolicSampleSize = Vst::SymbolicSampleSizes::kSample32;
		setup.processMode = Vst::ProcessModes::kRealtime;

		res = GetAudioProcessor()->setupProcessing(setup);
		if(res != kResultOk && res != kNotImplemented) { throw std::runtime_error("setupProcessing failed"); }
		status_ = Status::kSetupDone;
	}

	res = GetComponent()->setActive(true);
	if(res != kResultOk && res != kNotImplemented) { throw std::runtime_error("setActive failed"); }
	status_ = Status::kActivated;

	is_resumed_ = true;

	//! Some plugin (e.g., TyrellN6.vst3, podolski.vst3) need to create its plug view after the components is active.
	tresult const ret = CreatePlugView();
	has_editor_ = (ret == kResultOk);
		
	hwm::dout << "Latency samples : " << GetAudioProcessor()->getLatencySamples() << std::endl;

	//! doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html#af252fd721b195b793f3a5dfffc069401
	/*!
		@memo
		Informs the Plug-in about the processing state.
		This will be called before process calls (one or more) start with true and after with false.
		In this call the Plug-in should do only light operation (no memory allocation or big setup reconfiguration), this could be used to reset some buffers (like Delay line or Reverb).
	*/
	res = GetAudioProcessor()->setProcessing(true);
	if(res == kResultOk || res == kNotImplemented) {
		status_ = Status::kProcessing;
		is_processing_started_ = true;
	} else {
		hwm::dout << "Start processing failed : " << res << std::endl;
	}
}

void Vst3Plugin::Impl::Suspend()
{
	if(status_ == Status::kProcessing) {
		GetAudioProcessor()->setProcessing(false);
		status_ = Status::kActivated;
	}

	DeletePlugView();

	GetComponent()->setActive(false);
	status_ = Status::kSetupDone;
}

bool Vst3Plugin::Impl::IsResumed() const
{
	return is_resumed_.get();
}

void Vst3Plugin::Impl::SetBlockSize(int block_size)
{
	input_buses_.SetBlockSize(block_size);
	input_buses_.UpdateBufferHeads();
	output_buses_.SetBlockSize(block_size);
	output_buses_.UpdateBufferHeads();
	block_size_ = block_size;
}

void Vst3Plugin::Impl::SetSamplingRate(int sampling_rate)
{
	sampling_rate_ = sampling_rate;
}

void	Vst3Plugin::Impl::AddNoteOn(int note_number)
{
    auto lock = std::unique_lock(note_mutex_);
	Note note;
	note.note_number_ = note_number;
	note.note_state_ = Note::State::kNoteOn;
	notes_.push_back(note);
}

void	Vst3Plugin::Impl::AddNoteOff(int note_number)
{
	auto lock = std::unique_lock(note_mutex_);
	Note note;
	note.note_number_ = note_number;
	note.note_state_ = Note::State::kNoteOff;
	notes_.push_back(note);
}

size_t	Vst3Plugin::Impl::GetProgramCount() const
{
	return programs_.size();
}

String Vst3Plugin::Impl::GetProgramName(size_t index) const
{
	return programs_[index].name_;
}

/*!
	@note
	VoxengoのGlissEQでは
	parameters_.GetInfoByID(parameter_for_program_).stepCount == GetProgramCount()-1
	Wavesでは、
	parameters_.GetInfoByID(parameter_for_program_).stepCount == GetProgramCount()
	=> 当初はGetProgramCount()-1をstepCountとみなしてProgramIndexを変換しようと思ったが、
	上記の通りGetProgramCount()-1はstepCountと異なることがあり、WavesのプラグインでProgramIndexの指定がずれてしまうため、
	ちゃんとstepCountを元に変換して、どちらのプラグインでも対応できるようにした。
	IEditControllerのnormalizedParamToPlainを使用してもいいかもしれない。
*/
Vst::ParamValue
	Vst3Plugin::Impl::NormalizeProgramIndex(size_t index) const
{
	int step_count =
		(parameter_for_program_ != -1)
		?	parameters_.GetInfoByID(parameter_for_program_).stepCount
		:	GetProgramCount() - 1;

	return index / static_cast<Vst::ParamValue>(step_count);
}

size_t Vst3Plugin::Impl::DiscretizeProgramIndex(Vst::ParamValue value) const
{
	int step_count =
		(parameter_for_program_ != -1)
		?	parameters_.GetInfoByID(parameter_for_program_).stepCount
		:	GetProgramCount() - 1;

	return static_cast<size_t>(
		floor(std::min<Vst::ParamValue>(step_count, value * (step_count + 1)))
		);
}		

size_t	Vst3Plugin::Impl::GetProgramIndex() const
{
	return current_program_index_;
}

void	Vst3Plugin::Impl::SetProgramIndex(size_t index)
{
	if(parameter_for_program_ == -1) {
		//nothing to do
	} else {
		current_program_index_ = 
			DiscretizeProgramIndex(GetEditController()->getParamNormalized(parameter_for_program_));
	}

	if(index == current_program_index_) {
		return;
	}

	current_program_index_ = index;

	auto const &program = programs_[current_program_index_];
	auto const normalized = NormalizeProgramIndex(current_program_index_);

	param_value_changes_was_specified_ = false;

	//! `Controller`側、`Processor`側それぞれのコンポーネントにプログラム変更を通知
	if(parameter_for_program_ == -1) {
		GetEditController()->setParamNormalized(program.list_id_, normalized);
		EnqueueParameterChange(program.list_id_, normalized);
	} else {
		GetEditController()->setParamNormalized(parameter_for_program_, normalized);
		EnqueueParameterChange(parameter_for_program_, normalized);
	}
}

void Vst3Plugin::Impl::RestartComponent(Steinberg::int32 flags)
{
	//! `Controller`側のパラメータが変更された
	if((flags & Vst::RestartFlags::kParamValuesChanged)) {

		// nothing to do

	} else if((flags & Vst::RestartFlags::kReloadComponent)) {

		hwm::dout << "Should reload component" << std::endl;

        Steinberg::MemoryStream stream;
		//! ReloadComponentで行う内容はこれで合っているかどうか分からない。
		component_->getState(&stream);

		bool const is_resumed = IsResumed();
		if(is_resumed) {
			Suspend();
			Resume();
		}

		component_->setState(&stream);
	}
}

float ** Vst3Plugin::Impl::ProcessAudio(TransportInfo const &info, SampleCount duration)
{
	Vst::ProcessContext process_context = {};
	process_context.sampleRate = sampling_rate_;
	process_context.projectTimeSamples = info.sample_pos_;
    process_context.projectTimeMusic = info.ppq_pos_;
	process_context.tempo = info.tempo_;
	process_context.timeSigDenominator = info.time_sig_denom_;
	process_context.timeSigNumerator = info.time_sig_numer_;

    using Flags = Vst::ProcessContext::StatesAndFlags;
    
	process_context.state
    = (info.playing_ ? Flags::kPlaying : 0)
    | Flags::kProjectTimeMusicValid
    | Flags::kTempoValid
    | Flags::kTimeSigValid;
    
	Vst::EventList input_event_list;
	Vst::EventList output_event_list;
	{
        auto lock = std::unique_lock(note_mutex_);
		for(auto &note: notes_) {
			Vst::Event e;
			e.busIndex = 0;
			e.sampleOffset = 0;
			e.ppqPosition = process_context.projectTimeMusic;
			e.flags = Vst::Event::kIsLive;
			if(note.note_state_ == Note::kNoteOn) {
				e.type = Vst::Event::kNoteOnEvent;
				e.noteOn.channel = 0;
				e.noteOn.length = 0;
				e.noteOn.pitch = note.note_number_;
				e.noteOn.tuning = 0;
				e.noteOn.noteId = -1;
				e.noteOn.velocity = 100 / 127.0;
			} else if(note.note_state_ == Note::kNoteOff) {
				e.type = Vst::Event::kNoteOffEvent;
				e.noteOff.channel = 0;
				e.noteOff.pitch = note.note_number_;
				e.noteOff.tuning = 0;
				e.noteOff.noteId = -1;
				e.noteOff.velocity = 100 / 127.0;
			} else {
				continue;
			}
			input_event_list.addEvent(e);
		}
		notes_.clear();
	}

	std::vector<Vst::AudioBusBuffers> inputs(input_buses_.GetBusCount());
	for(size_t i = 0; i < inputs.size(); ++i) {
		inputs[i].channelBuffers32 = input_buses_.GetBus(i).data();
		inputs[i].numChannels = input_buses_.GetBus(i).channels();
		inputs[i].silenceFlags = false;

		if(inputs[i].numChannels != 0) {
			for(int ch = 0; ch < inputs[i].numChannels; ++ch) {
				for(int smp = 0; smp < duration; ++smp) {
					inputs[i].channelBuffers32[ch][smp] = 
						wave_data_[(wave_data_index_ + smp) % (int)process_context.sampleRate];
				}
			}
			wave_data_index_ = (wave_data_index_ + duration) % (int)process_context.sampleRate;
		}
	}

	std::vector<Vst::AudioBusBuffers> outputs(output_buses_.GetBusCount());
	for(size_t i = 0; i < outputs.size(); ++i) {
		outputs[i].channelBuffers32 = output_buses_.GetBus(i).data();
		outputs[i].numChannels = output_buses_.GetBus(i).channels();
		outputs[i].silenceFlags = false;
	}

	input_changes_.clearQueue();
	output_changes_.clearQueue();

	TakeParameterChanges(input_changes_);

	Vst::ProcessData process_data;
	process_data.processContext = &process_context;
	process_data.processMode = Vst::ProcessModes::kRealtime;
	process_data.symbolicSampleSize = Vst::SymbolicSampleSizes::kSample32;
	process_data.numSamples = duration;
	process_data.numInputs = inputs.size();
	process_data.numOutputs = outputs.size();
	process_data.inputs = inputs.data();
	process_data.outputs = outputs.data();
	process_data.inputEvents = &input_event_list;
	process_data.outputEvents = &output_event_list;
	process_data.inputParameterChanges = &input_changes_;
	process_data.outputParameterChanges = &output_changes_;

	GetAudioProcessor()->process(process_data);

	for(int i = 0; i < output_changes_.getParameterCount(); ++i) {
		auto *queue = output_changes_.getParameterData(i);
		if(queue && queue->getPointCount() > 0) {
			hwm::dout << "Output parameter count [" << i << "] : " << queue->getPointCount() << std::endl;
		}
	}

	return output_buses_.data();
}

//! TakeParameterChangesとの呼び出しはスレッドセーフ
void Vst3Plugin::Impl::EnqueueParameterChange(Vst::ParamID id, Vst::ParamValue value)
{
	auto lock = std::unique_lock(parameter_queue_mutex_);
	Steinberg::int32 parameter_index = 0;
	param_changes_queue_.addParameterData(id, parameter_index);
	auto *queue = param_changes_queue_.getParameterData(parameter_index);
	Steinberg::int32 point_index = 0;
	queue->addPoint(0, value, point_index);
}

//! EnqueueParameterChangeとの呼び出しはスレッドセーフ
void Vst3Plugin::Impl::TakeParameterChanges(Vst::ParameterChanges &dest)
{
	auto lock = std::unique_lock(parameter_queue_mutex_);

	for(size_t i = 0; i < param_changes_queue_.getParameterCount(); ++i) {
		auto *src_queue = param_changes_queue_.getParameterData(i);

		//! Boz Digital LabsのPANIPULATORではここでsrc_queueのgetPointCountが0になることがある。
		//! その時は単にaddParameterDataでdestにエントリが追加されるだけになってpointのデータが追加されず、
		//! ParameterChanges::getParameterDataでアクセス違反をが発生する。
		if(src_queue->getPointCount() == 0) {
			continue;
		}

		Steinberg::int32 index;
		dest.addParameterData(src_queue->getParameterId(), index);
		auto *dest_queue = dest.getParameterData(index);

		for(size_t v = 0; v < src_queue->getPointCount(); ++v) {
			Steinberg::int32 offset;
			Vst::ParamValue value;
			src_queue->getPoint(v, offset, value);
			Steinberg::int32 point_index;
			dest_queue->addPoint(offset, value, point_index);
		}
	}

	param_changes_queue_.clearQueue();
}

void Vst3Plugin::Impl::LoadPlugin(IPluginFactory *factory, ClassInfo const &info, FUnknown *host_context)
{
	LoadInterfaces(factory, info, host_context);
    
    auto component_handler = queryInterface<Vst::IComponentHandler>(host_context);
    assert(component_handler.is_right());
    
    Initialize(std::move(component_handler.right()));
}

void Vst3Plugin::Impl::LoadInterfaces(IPluginFactory *factory, ClassInfo const &info, FUnknown *host_context)
{
    auto cid = FUID::fromTUID(info.cid());
	auto maybe_component = createInstance<Vst::IComponent>(factory, cid);
	if(!maybe_component.is_right()) {
		throw Error(ErrorContext::kFactoryError, maybe_component.left());
	}
	auto component = std::move(maybe_component.right());
	tresult res;
	res = component->setIoMode(Vst::IoModes::kAdvanced);
	if(res != kResultOk && res != kNotImplemented) {
		throw Error(ErrorContext::kComponentError, res);
	}
	status_ = Status::kCreated;

	res = component->initialize(host_context);
	if(res != kResultOk) {
		throw Error(ErrorContext::kComponentError, res);
	}
	status_ = Status::kInitialized;

    HWM_SCOPE_EXIT([&component] {
		if(component) {
			component->terminate();
			component.reset();
		}
    });

	auto maybe_audio_processor = queryInterface<Vst::IAudioProcessor>(component);
	if(!maybe_audio_processor.is_right()) {
		throw Error(ErrorContext::kComponentError, maybe_audio_processor.left());
	}
	auto audio_processor = std::move(maybe_audio_processor.right());

	res = audio_processor->canProcessSampleSize(Vst::SymbolicSampleSizes::kSample32);
	if(res != kResultOk) {
		throw Error(ErrorContext::kAudioProcessorError, res);
	}

	// $(DOCUMENT_ROOT)/vstsdk360_22_11_2013_build_100/VST3%20SDK/doc/vstinterfaces/index.html
	// Although it is not recommended, it is possible to implement both, 
	// the processing part and the controller part in one component class.
	// The host tries to query the Steinberg::Vst::IEditController 
	// interface after creating an Steinberg::Vst::IAudioProcessor 
	// and on success uses it as controller. 
	auto maybe_edit_controller = queryInterface<Vst::IEditController>(component);
	bool edit_controller_is_created_new = false;
	if(!maybe_edit_controller.is_right()) {
		TUID controller_id;
		res = component->getControllerClassId(controller_id);
		if(res == kResultOk) {
            maybe_edit_controller = createInstance<Vst::IEditController>(factory, FUID::fromTUID(controller_id));
			if(maybe_edit_controller.is_right()) {
				edit_controller_is_created_new = true;
			} else {
				//! this plugin has no edit controller.
			}
		} else {
			if(res != kNoInterface && res != kNotImplemented) {
				throw Error(ErrorContext::kComponentError, res);
			}
		}
	}

	edit_controller_ptr_t edit_controller;
	if(maybe_edit_controller.is_right()) {
		edit_controller = std::move(maybe_edit_controller.right());
	}

	if(edit_controller_is_created_new) {
		res = edit_controller->initialize(host_context);
		if(res != kResultOk) {
			throw Error(ErrorContext::kEditControlError, res);
		}
	}

	HWM_SCOPE_EXIT([&edit_controller, edit_controller_is_created_new] {
		if(edit_controller_is_created_new && edit_controller) {
			edit_controller->terminate();
			edit_controller.reset();
		}
	});

	edit_controller2_ptr_t edit_controller2;
	if(edit_controller) {
		auto maybe_edit_controller2 = queryInterface<Vst::IEditController2>(edit_controller);

		if(maybe_edit_controller2.is_right()) {
			edit_controller2 = std::move(maybe_edit_controller2.right());
		}
	}

	plugin_info_ = info;
	component_ = std::move(component);
	audio_processor_ = std::move(audio_processor);
	edit_controller_ = std::move(edit_controller);
	edit_controller2_ = std::move(edit_controller2);
	edit_controller_is_created_new_ = edit_controller_is_created_new;
}

void Vst3Plugin::Impl::Initialize(std::unique_ptr<Vst::IComponentHandler, SelfReleaser> component_handler)
{
	tresult res;

	if(edit_controller_) {	
		if(component_handler) {
			res = edit_controller_->setComponentHandler(component_handler.get());
			if(res != kResultOk) {
                hwm::dout << "Can't set component handler" << std::endl;
			}
		}

		auto maybe_cpoint_component = queryInterface<Vst::IConnectionPoint>(component_);
		auto maybe_cpoint_edit_controller = queryInterface<Vst::IConnectionPoint>(edit_controller_);

		if (maybe_cpoint_component.is_right() && maybe_cpoint_edit_controller.is_right())
		{
			maybe_cpoint_component.right()->connect(maybe_cpoint_edit_controller.right().get());
			maybe_cpoint_edit_controller.right()->connect(maybe_cpoint_component.right().get());
		}

		// synchronize controller to component by using setComponentState
		MemoryStream stream;
		if (component_->getState (&stream) == kResultOk)
		{
			stream.seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekSet, 0);
			edit_controller_->setComponentState (&stream);
		}

		OutputBusInfo(component_.get(), edit_controller_.get());

		input_buses_.SetBusCount(component_->getBusCount(Vst::MediaTypes::kAudio, Vst::BusDirections::kInput));
		for(size_t i = 0; i < input_buses_.GetBusCount(); ++i) {
			Vst::BusInfo info;
			Vst::SpeakerArrangement arr;
			component_->getBusInfo(Vst::MediaTypes::kAudio, Vst::BusDirections::kInput, i, info);
			audio_processor_->getBusArrangement(Vst::BusDirections::kInput, i, arr);
			input_buses_.GetBus(i).SetChannels(info.channelCount, arr);
			component_->activateBus(Vst::MediaTypes::kAudio, Vst::BusDirections::kInput, i, true);
		}

		output_buses_.SetBusCount(component_->getBusCount(Vst::MediaTypes::kAudio, Vst::BusDirections::kOutput));
		for(size_t i = 0; i < output_buses_.GetBusCount(); ++i) {
			Vst::BusInfo info;
			Vst::SpeakerArrangement arr;
			component_->getBusInfo(Vst::MediaTypes::kAudio, Vst::BusDirections::kOutput, i, info);
			audio_processor_->getBusArrangement(Vst::BusDirections::kOutput, i, arr);
			output_buses_.GetBus(i).SetChannels(info.channelCount, arr);
			component_->activateBus(Vst::MediaTypes::kAudio, Vst::BusDirections::kOutput, i, true);
		}

		for(size_t i = 0; i < component_->getBusCount(Vst::MediaTypes::kEvent, Vst::BusDirections::kInput); ++i) {
			component_->activateBus(Vst::MediaTypes::kEvent, Vst::BusDirections::kInput, i, true);
		}
		for(size_t i = 0; i < component_->getBusCount(Vst::MediaTypes::kEvent, Vst::BusDirections::kOutput); ++i) {
			component_->activateBus(Vst::MediaTypes::kEvent, Vst::BusDirections::kOutput, i, true);
		}

		input_buses_.UpdateBufferHeads();
		output_buses_.UpdateBufferHeads();

		//! 可能であればこのあたりでIPlugViewを取得して、このプラグインがエディターを持っているかどうかを
		//! チェックしたかったが、いくつかのプラグイン(e.g., TyrellN6, Podolski)では
		//! IComponentがactivatedされる前にIPlugViewを取得するとクラッシュした。
		//! そのため、この段階ではIPlugViewは取得しない

		PrepareParameters();
		PrepareProgramList();

		input_changes_.setMaxParameters(parameters_.size());
		output_changes_.setMaxParameters(parameters_.size());
	}
}

tresult Vst3Plugin::Impl::CreatePlugView()
{
	if(!edit_controller_) {
		return kNoInterface;
	}

	//! プラグインによっては、既に別にPlugViewのインスタンスが作成されている時に
	//! 別のPlugViewを作成しようとするとクラッシュすることがある。
	assert(!plug_view_);

	plug_view_ = to_unique(edit_controller_->createView(Vst::ViewType::kEditor));

	if(!plug_view_) {
		auto maybe_view = queryInterface<IPlugView>(edit_controller_);
		if(maybe_view.is_right()) {
            plug_view_ = std::move(maybe_view.right());
		} else {
			return maybe_view.left();
		}
	}
    
#if defined(_MSC_VER)
    if(plug_view_->isPlatformTypeSupported(kPlatformTypeHWND) == kResultOk) {
        hwm::dout << "This plugin editor supports HWND" << std::endl;
    } else {
        return kNotImplemented;
    }
#else
    if(plug_view_->isPlatformTypeSupported(kPlatformTypeNSView) == kResultOk) {
        hwm::dout << "This plugin editor supports NS View" << std::endl;
    } else if(plug_view_->isPlatformTypeSupported(kPlatformTypeHIView) == kResultOk) {
        hwm::dout << "This plugin editor supports HI View" << std::endl;
    } else {
        return kNotImplemented;
    }
#endif
    
	return kResultOk;
}

void Vst3Plugin::Impl::DeletePlugView()
{
    assert(IsEditorOpened() == false);
	plug_view_.reset();
}

std::wstring
		Vst3Plugin::Impl::UnitInfoToString(Steinberg::Vst::UnitInfo const &info)
{
	std::wstringstream ss;
	ss	<< info.id
		<< L", " << info.name
		<< L", " << L"Parent: " << info.parentUnitId
		<< L", " << L"Program List ID: " << info.programListId;

	return ss.str();
}

std::wstring
		Vst3Plugin::Impl::ProgramListInfoToString(Steinberg::Vst::ProgramListInfo const &info)
{
	std::wstringstream ss;

	ss	<< info.id
		<< L", " << L"Program List Name: " << info.name
		<< L", " << L"Program Count: " << info.programCount;

	return ss.str();
}

void Vst3Plugin::Impl::PrepareParameters()
{
	for(Steinberg::int32 i = 0; i < edit_controller_->getParameterCount(); ++i) {
		Vst::ParameterInfo info = {};
		edit_controller_->getParameterInfo(i, info);
		parameters_.AddInfo(info);
	}
}

void Vst3Plugin::Impl::PrepareProgramList()
{
	parameter_for_program_ = -1;
	for(auto const &param_info: parameters_) {
		if((param_info.flags & Vst::ParameterInfo::ParameterFlags::kIsProgramChange) != 0) {
			parameter_for_program_ = param_info.id;
		}
	}

	auto maybe_unit_info = queryInterface<Vst::IUnitInfo>(edit_controller_);
	if(maybe_unit_info.is_right()) {
		unit_info_ = std::move(maybe_unit_info.right());
	} else {
		hwm::dout << "No Unit Info Interface." << std::endl;
		return;
	}

	OutputUnitInfo(*unit_info_);

	tresult res;

	if(unit_info_->getUnitCount() == 0) {
		hwm::dout << "No Unit Info." << std::endl;
		return;
	}

	std::vector<Steinberg::Vst::ProgramListInfo> program_list_info_list;

	std::vector<ProgramInfo> tmp_programs;

	//! 使用するプログラムリストを選択するために、最初にprogramListIdが有効なUnitを取得。
	//! ただし再帰的に検索はしていない
	Steinberg::Vst::ProgramListID program_list_id = Steinberg::Vst::kNoProgramListId;
	for(int i = 0; i < unit_info_->getUnitCount(); ++i) {
		Vst::UnitInfo uinfo;
		res = unit_info_->getUnitInfo(i, uinfo);

		if(uinfo.programListId == Steinberg::Vst::kNoProgramListId) {
			continue;
		}

		unit_info_->selectUnit(uinfo.id);
		program_list_id = uinfo.programListId;
		break;
	}

	if(program_list_id == Steinberg::Vst::kNoProgramListId) {
		hwm::dout << "No Program List assigned to Units." << std::endl;
	}

	//! 使用するプログラムリストが決まったのでそのリストからprograms_を構築
	size_t const prog_list_count = unit_info_->getProgramListCount();
	for(size_t npl = 0; npl < prog_list_count; ++npl) {
		Vst::ProgramListInfo plinfo;
		res = unit_info_->getProgramListInfo(npl, plinfo);

		if(plinfo.id != program_list_id) {
			continue;
		}

		for(int i = 0; i < plinfo.programCount; ++i) {
			Steinberg::Vst::String128 name_buf;
			unit_info_->getProgramName(plinfo.id, i, name_buf);

			ProgramInfo prginfo;

            prginfo.name_ = hwm::to_wstr(name_buf);
			prginfo.list_id_ = plinfo.id;
			prginfo.index_ = i;

			tmp_programs.push_back(prginfo);
		}
	}

	programs_.swap(tmp_programs);
}

void Vst3Plugin::Impl::UnloadPlugin()
{
	if(status_ == Status::kActivated || status_ == Status::kProcessing) {
		Suspend();
	}
    
    auto maybe_cpoint_component = queryInterface<Vst::IConnectionPoint>(component_);
    auto maybe_cpoint_edit_controller = queryInterface<Vst::IConnectionPoint>(edit_controller_);
    
    if (maybe_cpoint_component.is_right() && maybe_cpoint_edit_controller.is_right()) {
        maybe_cpoint_component.right()->disconnect(maybe_cpoint_edit_controller.right().get());
        maybe_cpoint_edit_controller.right()->disconnect(maybe_cpoint_component.right().get());
    }
    
    edit_controller_->setComponentHandler(nullptr);

	unit_info_.reset();
	plug_view_.reset();

	if(edit_controller_ && edit_controller_is_created_new_) {
		edit_controller_->terminate();
	}

	edit_controller2_.reset();

	edit_controller_.reset();
	audio_processor_.reset();

    if(component_) {
        component_->terminate();
    }
	component_.reset();
}

void Vst3Plugin::Impl::OutputUnitInfo(Steinberg::Vst::IUnitInfo &info_interface)
{
	hwm::wdout << "--- Output Unit Info ---" << std::endl;

	for(size_t i = 0; i < info_interface.getUnitCount(); ++i) {
		Steinberg::Vst::UnitInfo unit_info {};
		info_interface.getUnitInfo(i, unit_info);
		hwm::wdout << L"[" << i << L"] " << UnitInfoToString(unit_info) << std::endl;
	}

	hwm::wdout << L"Selected Unit: " << info_interface.getSelectedUnit() << std::endl;

	hwm::wdout << "--- Output Program List Info ---" << std::endl;

	for(size_t i = 0; i < info_interface.getProgramListCount(); ++i) {
		Steinberg::Vst::ProgramListInfo program_list_info {};
		tresult res = info_interface.getProgramListInfo(i, program_list_info);
		if(res != Steinberg::kResultOk) {
			hwm::wdout << "Getting program list info failed." << std::endl;
			break;
		}

		hwm::wdout << L"[" << i << L"] " << ProgramListInfoToString(program_list_info) << std::endl;

		for(size_t program_index = 0; program_index < program_list_info.programCount; ++program_index) {

			hwm::wdout << L"\t[" << program_index << L"] ";

			Steinberg::Vst::String128 name;
			info_interface.getProgramName(program_list_info.id, program_index, name);

			hwm::wdout << name;

			Steinberg::Vst::CString attrs[] { 
				Steinberg::Vst::PresetAttributes::kPlugInName,
				Steinberg::Vst::PresetAttributes::kPlugInCategory,
				Steinberg::Vst::PresetAttributes::kInstrument,
				Steinberg::Vst::PresetAttributes::kStyle,
				Steinberg::Vst::PresetAttributes::kCharacter,
				Steinberg::Vst::PresetAttributes::kStateType,
				Steinberg::Vst::PresetAttributes::kFilePathStringType };

			for(auto attr: attrs) {
				Steinberg::Vst::String128 attr_value = {};
				info_interface.getProgramInfo(program_list_info.id, program_index, attr, attr_value);

				hwm::wdout << L", " << attr << L": " << attr_value;
			}

			if(info_interface.hasProgramPitchNames(program_list_info.id, program_index) == Steinberg::kResultTrue) {
				Steinberg::Vst::String128 pitch_name = {};
				Steinberg::int16 const pitch_center = 0x2000;
				info_interface.getProgramPitchName(program_list_info.id, program_index, pitch_center, pitch_name);

				hwm::wdout << L", " << pitch_name;
			} else {
				hwm::wdout << L", No Pitch Name";
			}

			hwm::wdout << std::endl;
		}
	}
}

std::wstring Vst3Plugin::Impl::BusInfoToString(Vst::BusInfo &bus)
{
	std::wstringstream ss;

	ss	<< bus.name 
		<< L", " << (bus.mediaType == Vst::MediaTypes::kAudio ? L"Audio" : L"Midi")
		<< L", " << (bus.direction == Vst::BusDirections::kInput ? L"Input" : L"Output")
		<< L", " << (bus.busType == Vst::BusTypes::kMain ? L"Main Bus" : L"Aux Bus")
		<< L", " << L"Channel " << bus.channelCount
		<< L", " << (((bus.flags & bus.kDefaultActive) != 0) ? L"Default Active" : L"Not Default Active");

	return ss.str();
}
	
std::wstring Vst3Plugin::Impl::BusUnitInfoToString(int bus_index, Vst::BusInfo &bus, Vst::IUnitInfo &unit)
{
	if(bus.channelCount == 0) {
		return L"No channels for units";
	}

	std::wstringstream ss;
	for(int ch = 0; ch < bus.channelCount; ++ch) {
		Vst::UnitID unit_id;
		tresult result = unit.getUnitByBus(bus.mediaType, bus.direction, bus_index, ch, unit_id);
		if(result != kResultOk) {
			ss.str(L"Can't get unit by bus");
			break;
		}
		ss << L"[" << unit_id << L"]";
	}
	return ss.str();
}

void Vst3Plugin::Impl::OutputBusInfoImpl(Vst::IComponent *component, Vst::IUnitInfo *unit, Vst::MediaType media_type, Vst::BusDirection bus_direction)
{
	int32 const num_component = component->getBusCount(media_type, bus_direction);
	if(num_component == 0) {
		hwm::wdout << "No bus for this type." << std::endl;
		return;
	}
	for(int32 i = 0; i < num_component; ++i) {
		Vst::BusInfo info;
		component->getBusInfo(media_type, bus_direction, i, info);

		auto const bus_info_str = BusInfoToString(info);
		auto const bus_unit_info_str =
			(unit ? BusUnitInfoToString(i, info, *unit) : L"No Assigned Unit");
		hwm::wdout << L"[" << i << L"] " << bus_info_str << L", " << bus_unit_info_str << std::endl;
	}
}

void Vst3Plugin::Impl::OutputBusInfo(Vst::IComponent *component, Vst::IEditController *edit_controller)
{
	auto maybe_unit_info = queryInterface<Vst::IUnitInfo>(edit_controller_);
	auto unit = (maybe_unit_info.is_right() ? maybe_unit_info.right().get() : nullptr);

	hwm::dout << "-- output bus info --" << std::endl;
	hwm::dout << "[Audio Input]" << std::endl;
	OutputBusInfoImpl(component, unit, Vst::MediaTypes::kAudio, Vst::BusDirections::kInput);
	hwm::dout << "[Audio Output]" << std::endl;
	OutputBusInfoImpl(component, unit, Vst::MediaTypes::kAudio, Vst::BusDirections::kOutput);
	hwm::dout << "[Event Input]" << std::endl;
	OutputBusInfoImpl(component, unit, Vst::MediaTypes::kEvent, Vst::BusDirections::kInput);
	hwm::dout << "[Event Output]" << std::endl;
	OutputBusInfoImpl(component, unit, Vst::MediaTypes::kEvent, Vst::BusDirections::kOutput);
}

NS_HWM_END
