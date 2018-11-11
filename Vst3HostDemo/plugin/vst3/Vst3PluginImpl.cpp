#include "Vst3PluginImpl.hpp"

#include <algorithm>
#include <numeric>
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
#include "Vst3Debug.hpp"

using namespace Steinberg;

NS_HWM_BEGIN

extern void* GetWindowRef(NSView *view);

namespace {
    std::array<float *, 32> kDummyChannelData;
}

void Vst3Plugin::Impl::AudioBusesInfo::Initialize(Impl *owner, Vst::BusDirection dir)
{
    owner_ = owner;
    
    auto &comp = owner_->component_;
    auto &proc = owner_->audio_processor_;
    
    auto const media = Vst::MediaTypes::kAudio;
    
    size_t const num_buses = comp->getBusCount(media, dir);
    bus_infos_.resize(num_buses);
    
    tresult ret = kResultTrue;
    for(size_t i = 0; i < num_buses; ++i) {
        Vst::BusInfo bi;
        ret = comp->getBusInfo(media, dir, i, bi);
        if(ret != kResultTrue) { throw std::runtime_error("Failed to get BusInfo"); }
        
        bus_infos_[i].bus_info_ = bi;
        bus_infos_[i].is_active_ = (bi.flags & Vst::BusInfo::kDefaultActive) != 0;
        
        Vst::SpeakerArrangement arr;
        auto ret = proc->getBusArrangement(dir, i, arr);
        if(ret != kResultTrue) { throw std::runtime_error("Failed to get SpeakerArrangement"); }
        bus_infos_[i].speaker_ = arr;
    }
    
    UpdateBusBuffers();
}

size_t Vst3Plugin::Impl::AudioBusesInfo::GetNumBuses() const
{
    return bus_infos_.size();
}

Vst3Plugin::Impl::BusInfoEx const & Vst3Plugin::Impl::AudioBusesInfo::GetBusInfo(size_t bus_index) const
{
    assert(bus_index < GetNumBuses());
    return bus_infos_[bus_index];
}

size_t Vst3Plugin::Impl::AudioBusesInfo::GetNumChannels() const
{
    return std::accumulate(bus_infos_.begin(),
                           bus_infos_.end(),
                           0,
                           [](size_t sum, auto const &info) {
                               return sum + info.bus_info_.channelCount;
                           });
}

size_t Vst3Plugin::Impl::AudioBusesInfo::GetNumActiveChannels() const
{
    return std::accumulate(bus_infos_.begin(),
                           bus_infos_.end(),
                           0,
                           [](size_t sum, auto const &info) {
                               return sum + (info.is_active_ ? info.bus_info_.channelCount : 0);
                           });
}

bool Vst3Plugin::Impl::AudioBusesInfo::IsActive(size_t bus_index) const
{
    return GetBusInfo(bus_index).is_active_;
}

void Vst3Plugin::Impl::AudioBusesInfo::SetActive(size_t bus_index, bool state)
{
    assert(bus_index < GetNumBuses());
    
    auto &comp = owner_->component_;
    auto const result = comp->activateBus(Vst::MediaTypes::kAudio, dir_, bus_index, state);
    if(result != kResultTrue) {
        throw std::runtime_error("Failed to activate a bus");
    }
    
    bus_infos_[bus_index].is_active_ = state;
}

//! @return true if this speaker arrangement is accepted to the plugin successfully,
//! false otherwise.
bool Vst3Plugin::Impl::AudioBusesInfo::SetSpeakerArrangement(size_t bus_index, Vst::SpeakerArrangement arr)
{
    assert(bus_index < GetNumBuses());
    
    auto get_speakers = [](AudioBusesInfo const &buses_info) {
        std::vector<Vst::SpeakerArrangement> arrs;
        size_t const num = buses_info.GetNumBuses();
        for(size_t i = 0; i < num; ++i) {
            arrs.push_back(buses_info.GetBusInfo(i).speaker_);
        }
        
        return arrs;
    };
    
    auto input_arrs = get_speakers(owner_->input_buses_info_);
    auto output_arrs = get_speakers(owner_->output_buses_info_);
    
    auto &my_arrs = (dir_ == Vst::BusDirections::kInput ? input_arrs : output_arrs);
    my_arrs[bus_index] = arr;
    
    auto &proc = owner_->audio_processor_;
    auto const result = proc->setBusArrangements(input_arrs.data(), input_arrs.size(),
                                                 output_arrs.data(), output_arrs.size());
    
    if(result != kResultTrue) {
        hwm::dout << "setBusArrangement failed" << std::endl;
        return false;
    }
    
    bus_infos_[bus_index].speaker_ = arr;
    UpdateBusBuffers();
    return true;
}

Vst::AudioBusBuffers * Vst3Plugin::Impl::AudioBusesInfo::GetBusBuffers()
{
    return bus_buffers_.data();
}

void Vst3Plugin::Impl::AudioBusesInfo::UpdateBusBuffers()
{
    bus_buffers_.clear();
    
    for(auto const &bi: bus_infos_) {
        Vst::AudioBusBuffers tmp = {};
        tmp.numChannels = bi.bus_info_.channelCount;
        bus_buffers_.push_back(tmp);
    }
}

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

    input_events_.setMaxSize(128);
    output_events_.setMaxSize(128);
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

Vst3Plugin::Impl::AudioBusesInfo & Vst3Plugin::Impl::GetInputBuses()
{
    return input_buses_info_;
}

Vst3Plugin::Impl::AudioBusesInfo const & Vst3Plugin::Impl::GetInputBuses() const
{
    return input_buses_info_;
}

Vst3Plugin::Impl::AudioBusesInfo & Vst3Plugin::Impl::GetOutputBuses()
{
    return output_buses_info_;
}

Vst3Plugin::Impl::AudioBusesInfo const & Vst3Plugin::Impl::GetOutputBuses() const
{
    return output_buses_info_;
}

size_t Vst3Plugin::Impl::GetNumParameters() const
{
    return edit_controller_->getParameterCount();
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
    
    input_buffer_.resize(input_buses_info_.GetNumActiveChannels(), block_size_);
    output_buffer_.resize(output_buses_info_.GetNumActiveChannels(), block_size_);
    
    auto prepare_bus_buffers = [&](AudioBusesInfo &buses, Buffer<float> &buffer) {
        assert(buffer.channels() == buses.GetNumActiveChannels());
        
        auto data = buffer.data();
        auto *bus_buffers = buses.GetBusBuffers();
        for(int i = 0; i < buses.GetNumBuses(); ++i) {
            if(buses.IsActive(i)) {
                bus_buffers[i].channelBuffers32 = data;
                data += bus_buffers[i].numChannels;
            } else {
                bus_buffers[i].channelBuffers32 = kDummyChannelData.data();
            }
        }
    };
    
    prepare_bus_buffers(input_buses_info_, input_buffer_);
    prepare_bus_buffers(output_buses_info_, output_buffer_);

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
	block_size_ = block_size;
}

void Vst3Plugin::Impl::SetSamplingRate(int sampling_rate)
{
	sampling_rate_ = sampling_rate;
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

void Vst3Plugin::Impl::Process(ProcessInfo pi)
{
    assert(pi.ti_);
    auto &ti = *pi.ti_;
	Vst::ProcessContext process_context = {};
	process_context.sampleRate = sampling_rate_;
	process_context.projectTimeSamples = ti.sample_pos_;
    process_context.projectTimeMusic = ti.ppq_pos_;
	process_context.tempo = ti.tempo_;
	process_context.timeSigDenominator = ti.time_sig_denom_;
	process_context.timeSigNumerator = ti.time_sig_numer_;

    using Flags = Vst::ProcessContext::StatesAndFlags;
    
	process_context.state
    = (ti.playing_ ? Flags::kPlaying : 0)
    | Flags::kProjectTimeMusicValid
    | Flags::kTempoValid
    | Flags::kTimeSigValid;

    input_events_.clear();
    output_events_.clear();
    
    for(auto &note: pi.notes_) {
        Vst::Event e;
        e.busIndex = 0;
        e.sampleOffset = note.GetOffset();
        e.ppqPosition = process_context.projectTimeMusic;
        e.flags = Vst::Event::kIsLive;
        if(note.IsNoteOn()) {
            e.type = Vst::Event::kNoteOnEvent;
            e.noteOn.channel = note.GetChannel();
            e.noteOn.length = 0;
            e.noteOn.pitch = note.GetPitch();
            e.noteOn.tuning = 0;
            e.noteOn.noteId = -1;
            e.noteOn.velocity = note.GetVelocity() / 127.0;
        } else {
            e.type = Vst::Event::kNoteOffEvent;
            e.noteOff.channel = note.GetChannel();
            e.noteOff.pitch = note.GetPitch();
            e.noteOff.tuning = 0;
            e.noteOff.noteId = -1;
            e.noteOff.velocity = note.GetVelocity();
        }
        input_events_.addEvent(e);
    }
    
    input_buffer_.fill();
	
    auto copy_buffer = [&](auto const &src, auto &dest,
                           SampleCount length_to_copy,
                           SampleCount src_offset, SampleCount dest_offset)
    {
        size_t const min_ch = std::min(src.channels(), dest.channels());
        if(min_ch == 0) { return; }

        assert(src.samples() - src_offset >= length_to_copy);
        assert(dest.samples() - dest_offset >= length_to_copy);
        
        for(size_t ch = 0; ch < min_ch; ++ch) {
            std::copy_n(src.data()[ch] + src_offset, length_to_copy, dest.data()[ch] + dest_offset);
        }
    };
    
    copy_buffer(pi.input_.buffer_, input_buffer_,
                pi.frame_length_,
                pi.input_.sample_offset_, 0);

	input_params_.clearQueue();
	output_params_.clearQueue();

	TakeParameterChanges(input_params_);

	Vst::ProcessData process_data;
	process_data.processContext = &process_context;
	process_data.processMode = Vst::ProcessModes::kRealtime;
	process_data.symbolicSampleSize = Vst::SymbolicSampleSizes::kSample32;
	process_data.numSamples = pi.frame_length_;
    process_data.numInputs = input_buses_info_.GetNumBuses();
	process_data.numOutputs = output_buses_info_.GetNumBuses();
    process_data.inputs = input_buses_info_.GetBusBuffers();
	process_data.outputs = output_buses_info_.GetBusBuffers();
	process_data.inputEvents = &input_events_;
	process_data.outputEvents = &output_events_;
	process_data.inputParameterChanges = &input_params_;
	process_data.outputParameterChanges = &output_params_;

	GetAudioProcessor()->process(process_data);
    
    copy_buffer(output_buffer_, pi.output_.buffer_,
                pi.frame_length_,
                0, pi.output_.sample_offset_);

	for(int i = 0; i < output_params_.getParameterCount(); ++i) {
		auto *queue = output_params_.getParameterData(i);
		if(queue && queue->getPointCount() > 0) {
			hwm::dout << "Output parameter count [" << i << "] : " << queue->getPointCount() << std::endl;
		}
	}
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

    if(maybe_edit_controller.is_right() == false) {
        //! this plugin has no edit controller. this host rejects such a plugin.
        throw Error(ErrorContext::kComponentError, kNoInterface);
    }
    
	edit_controller_ptr_t edit_controller = std::move(maybe_edit_controller.right());

	if(edit_controller_is_created_new) {
		res = edit_controller->initialize(host_context);
		if(res != kResultOk) {
			throw Error(ErrorContext::kEditControllerError, res);
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

void Vst3Plugin::Impl::Initialize(vstma_unique_ptr<Vst::IComponentHandler> component_handler)
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
        
        auto maybe_unit_info = queryInterface<Vst::IUnitInfo>(edit_controller_);
        if(maybe_unit_info.is_right()) {
            unit_info_ = std::move(maybe_unit_info.right());
        } else {
            hwm::dout << "No UnitInfo Interface." << std::endl;
            return;
        }

        OutputBusInfo(component_.get(), edit_controller_.get(), unit_info_.get());

        input_buses_info_.Initialize(this, Vst::BusDirections::kInput);
        output_buses_info_.Initialize(this, Vst::BusDirections::kOutput);

		//! 可能であればこのあたりでIPlugViewを取得して、このプラグインがエディターを持っているかどうかを
		//! チェックしたかったが、いくつかのプラグイン(e.g., TyrellN6, Podolski)では
		//! IComponentがactivateされる前にIPlugViewを取得するとクラッシュした。
		//! そのため、この段階ではIPlugViewは取得しない

		PrepareParameters();
		PrepareProgramList();

		input_params_.setMaxParameters(parameters_.size());
		output_params_.setMaxParameters(parameters_.size());
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

	OutputUnitInfo(unit_info_.get());

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

NS_HWM_END
