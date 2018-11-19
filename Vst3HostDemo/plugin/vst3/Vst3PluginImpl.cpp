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

void Vst3Plugin::Impl::AudioBusesInfo::Initialize(Impl *owner, Vst::BusDirection dir)
{
    owner_ = owner;
    dir_ = dir;
    
    auto &comp = owner_->component_;
    auto &proc = owner_->audio_processor_;
    
    auto const media = Vst::MediaTypes::kAudio;
    
    size_t const num_buses = comp->getBusCount(media, dir);
    bus_infos_.resize(num_buses);
    
    tresult ret = kResultTrue;
    for(size_t i = 0; i < num_buses; ++i) {
        Vst::BusInfo vbi;
        ret = comp->getBusInfo(media, dir, i, vbi);
        if(ret != kResultTrue) { throw std::runtime_error("Failed to get BusInfo"); }
        
        BusInfo bi;
        bi.bus_type_ = vbi.busType;
        bi.channel_count_ = vbi.channelCount;
        bi.direction_ = vbi.direction;
        bi.is_default_active_ = (vbi.flags & Vst::BusInfo::kDefaultActive) != 0;
        bi.media_type_ = vbi.mediaType;
        bi.name_ = to_wstr(vbi.name);
        bi.is_active_ = bi.is_default_active_;
        
        Vst::SpeakerArrangement arr;
        auto ret = proc->getBusArrangement(dir, i, arr);
        if(ret != kResultTrue) { throw std::runtime_error("Failed to get SpeakerArrangement"); }
        bi.speaker_ = arr;
        
        bus_infos_[i] = bi;
    }
    
    UpdateBusBuffers();
}

size_t Vst3Plugin::Impl::AudioBusesInfo::GetNumBuses() const
{
    return bus_infos_.size();
}

Vst3Plugin::BusInfo const & Vst3Plugin::Impl::AudioBusesInfo::GetBusInfo(UInt32 bus_index) const
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
                               return sum + info.channel_count_;
                           });
}

size_t Vst3Plugin::Impl::AudioBusesInfo::GetNumActiveChannels() const
{
    return std::accumulate(bus_infos_.begin(),
                           bus_infos_.end(),
                           0,
                           [](size_t sum, auto const &info) {
                               return sum + (info.is_active_ ? info.channel_count_ : 0);
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
    bus_infos_[bus_index].channel_count_ = Vst::SpeakerArr::getChannelCount(arr);
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
        tmp.numChannels = bi.channel_count_;
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

Vst3Plugin::Impl::ParameterInfoList & Vst3Plugin::Impl::GetParameterInfoList()
{
    return parameter_info_list_;
}

Vst3Plugin::Impl::ParameterInfoList const & Vst3Plugin::Impl::GetParameterInfoList() const
{
    return parameter_info_list_;
}

Vst3Plugin::Impl::UnitInfoList & Vst3Plugin::Impl::GetUnitInfoList()
{
    return unit_info_list_;
}

Vst3Plugin::Impl::UnitInfoList const & Vst3Plugin::Impl::GetUnitInfoList() const
{
    return unit_info_list_;
}

Vst3Plugin::Impl::AudioBusesInfo & Vst3Plugin::Impl::GetBusesInfo(Vst::BusDirection dir)
{
    if(dir == Vst::BusDirections::kInput) {
        return input_buses_info_;
    } else {
        return output_buses_info_;
    }
}

Vst3Plugin::Impl::AudioBusesInfo const & Vst3Plugin::Impl::GetBusesInfo(Vst::BusDirection dir) const
{
    if(dir == Vst::BusDirections::kInput) {
        return input_buses_info_;
    } else {
        return output_buses_info_;
    }
}

UInt32 Vst3Plugin::Impl::GetNumParameters() const
{
    return edit_controller_->getParameterCount();
}

Vst::ParamValue Vst3Plugin::Impl::GetParameterValueByIndex(UInt32 index) const
{
    auto id = GetParameterInfoList().GetItemByIndex(index).id_;
    return GetParameterValueByID(id);
}

Vst::ParamValue Vst3Plugin::Impl::GetParameterValueByID(Vst::ParamID id) const
{
    return edit_controller_->getParamNormalized(id);
}

UInt32 Vst3Plugin::Impl::GetProgramIndex(Vst::UnitID unit_id) const
{
    auto const &unit_info = GetUnitInfoList().GetItemByID(unit_id);
    UInt32 const size = unit_info.program_list_.programs_.size();
    ParamID const param_id = unit_info.program_change_param_;
    
    if(param_id == Vst::kNoParamId || size == 0) {
        return -1;
    }
    
    auto const normalized_value = GetEditController()->getParamNormalized(param_id);
    auto const plain = (UInt32)std::round(normalized_value * (size-1));
    assert(plain < size);
    
    return plain;
}

void Vst3Plugin::Impl::SetProgramIndex(UInt32 index, Vst::UnitID unit_id)
{
    auto const &unit_info = GetUnitInfoList().GetItemByID(unit_id);
    UInt32 const size = unit_info.program_list_.programs_.size();
    ParamID const param_id = unit_info.program_change_param_;
    
    assert(index < size);
    
    if(param_id == Vst::kNoParamId || size == 0) {
        return;
    }
    
    // Wavesでは、このパラメータに対するstepCountがsizeと同一になっていて、
    // VST3のドキュメントにあるようにstepCount+1でnormalied_valueを計算すると、ズレが発生してしまう。
    // そのため、プログラムのindexに関してはstepCountは使用せず、プログラム数からnormalized_valueを計算する。
    auto const normalized_value = index / (double)size;
    
    GetEditController()->setParamNormalized(unit_info.program_change_param_, normalized_value);
    PushBackParameterChange(unit_info.program_change_param_, normalized_value);
}

bool Vst3Plugin::Impl::HasEditor() const
{
	assert(component_);
	assert(is_resumed_);
	return has_editor_.get();
}

bool Vst3Plugin::Impl::OpenEditor(WindowHandle parent, IPlugFrame *plug_frame)
{
    assert(HasEditor());
    
    // この関数を呼び出す前に、GetPreferredRect()から返る幅と高さで、
    // parentはのウィンドウサイズを設定しておくこと。
    // さもなければ、プラグインによっては正しく描画が行われない。
    
    tresult res;
    
    assert(plug_frame);

    plug_view_->setFrame(plug_frame);
    
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
    
    auto prepare_bus_buffers = [&](AudioBusesInfo &buses, UInt32 block_size, Buffer<float> &buffer) {
        buffer.resize(buses.GetNumChannels(), block_size);
        
        auto data = buffer.data();
        auto *bus_buffers = buses.GetBusBuffers();
        for(int i = 0; i < buses.GetNumBuses(); ++i) {
            auto &buffer = bus_buffers[i];
            // AudioBusBufferのドキュメントには、非アクティブなBusについては各チャンネルのバッファのアドレスがnullでもいいという記述があるが、
            // これの指す意味があまりわからない。
            // 試しにここで、非アクティブなBusのchannelBuffers32にnumChannels個のnullptrからなる有効な配列を渡しても、
            // hostcheckerプラグインでエラー扱いになってしまう。
            // 詳細が不明なため、すべてのBusのすべてのチャンネルに対して、有効なバッファを割り当てるようにする。
            buffer.channelBuffers32 = data;
            buffer.silenceFlags = (buses.IsActive(i) ? 0 : -1);
            data += buffer.numChannels;
        }
    };
    
    prepare_bus_buffers(input_buses_info_, block_size_, input_buffer_);
    prepare_bus_buffers(output_buses_info_, block_size_, output_buffer_);

	res = GetComponent()->setActive(true);
	if(res != kResultOk && res != kNotImplemented) { throw std::runtime_error("setActive failed"); }
	status_ = Status::kActivated;

	is_resumed_ = true;
		
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
            e.noteOff.velocity = note.GetVelocity() / 127.0;
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

	PopFrontParameterChanges(input_params_);

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
            hwm::dout << "Output parameter count [{}] : {}"_format(i, queue->getPointCount()) << std::endl;
		}
	}
}

void Vst3Plugin::Impl::PushBackParameterChange(Vst::ParamID id, Vst::ParamValue value)
{
	auto lock = std::unique_lock(parameter_queue_mutex_);
	Steinberg::int32 parameter_index = 0;
	auto *queue = param_changes_queue_.addParameterData(id, parameter_index);
	Steinberg::int32 ref_point_index = 0;
	queue->addPoint(0, value, ref_point_index);
    (void)ref_point_index; // currently unused.
}

void Vst3Plugin::Impl::PopFrontParameterChanges(Vst::ParameterChanges &dest)
{
	auto lock = std::unique_lock(parameter_queue_mutex_);

	for(size_t i = 0; i < param_changes_queue_.getParameterCount(); ++i) {
		auto *src_queue = param_changes_queue_.getParameterData(i);
        auto const src_id = src_queue->getParameterId();
        
        if(src_id == Vst::kNoParamId || src_queue->getPointCount() == 0) {
            continue;
        }

		Steinberg::int32 ref_queue_index;
		auto dest_queue = dest.addParameterData(src_queue->getParameterId(), ref_queue_index);

		for(size_t v = 0; v < src_queue->getPointCount(); ++v) {
			Steinberg::int32 ref_offset;
			Vst::ParamValue ref_value;
			tresult result = src_queue->getPoint(v, ref_offset, ref_value);
            if(result != kResultTrue) { continue; }
            
			Steinberg::int32 ref_point_index;
			dest_queue->addPoint(ref_offset, ref_value, ref_point_index);
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
        
        OutputParameterInfo(edit_controller_.get());
        
        auto maybe_unit_info = queryInterface<Vst::IUnitInfo>(edit_controller_);
        if(maybe_unit_info.is_right()) {
            unit_handler_ = std::move(maybe_unit_info.right());
            OutputUnitInfo(unit_handler_.get());
            
            if(unit_handler_->getUnitCount() == 0) {
                // Treat as this plugin has no IUnitInfo
                unit_handler_.reset();
            }
        }
        
        if(!unit_handler_) {
            hwm::dout << "This Plugin has no IUnitInfo interafaces." << std::endl;
        }

        OutputBusInfo(component_.get(), edit_controller_.get(), unit_handler_.get());

        input_buses_info_.Initialize(this, Vst::BusDirections::kInput);
        output_buses_info_.Initialize(this, Vst::BusDirections::kOutput);

        tresult const ret = CreatePlugView();
        has_editor_ = (ret == kResultOk);

		PrepareParameters();
		PrepareUnitInfo();

		input_params_.setMaxParameters(parameter_info_list_.size());
		output_params_.setMaxParameters(parameter_info_list_.size());
	}
}

tresult Vst3Plugin::Impl::CreatePlugView()
{
	if(!edit_controller_) {
		return kNoInterface;
	}

    //! 一つのプラグインから複数のPlugViewを構築してはいけない。
    //! (実際TyrellやPodolskiなどいくつかのプラグインでクラッシュする)
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
		Vst::ParameterInfo vpi = {};
		edit_controller_->getParameterInfo(i, vpi);
        ParameterInfo pi;
        pi.id_                  = vpi.id;
        pi.title_               = to_wstr(vpi.title);
        pi.short_title_         = to_wstr(vpi.shortTitle);
        pi.units_               = to_wstr(vpi.units);
        pi.step_count_          = vpi.stepCount;
        pi.default_normalized_value_ = vpi.defaultNormalizedValue;
        pi.unit_id_             = vpi.unitId;
        pi.can_automate_        = (vpi.flags & Vst::ParameterInfo::kCanAutomate) != 0;
        pi.is_readonly_         = (vpi.flags & Vst::ParameterInfo::kIsReadOnly) != 0;
        pi.is_wrap_aound_       = (vpi.flags & Vst::ParameterInfo::kIsWrapAround) != 0;
        pi.is_list_             = (vpi.flags & Vst::ParameterInfo::kIsList) != 0;
        pi.is_program_change_   = (vpi.flags & Vst::ParameterInfo::kIsProgramChange) != 0;
        pi.is_bypass_           = (vpi.flags & Vst::ParameterInfo::kIsBypass) != 0;
        
		parameter_info_list_.AddItem(pi);
	}
}

Vst3Plugin::ProgramList CreateProgramList(Vst::IUnitInfo *unit_handler, Vst::ProgramListID program_list_id) {
    Vst3Plugin::ProgramList pl;
    auto num = unit_handler->getProgramListCount();
    for(int i = 0; i < num; ++i) {
        Vst::ProgramListInfo info;
        unit_handler->getProgramListInfo(i, info);
        if(info.id != program_list_id) { continue; }
        
        pl.id_ = program_list_id;
        pl.name_ = to_wstr(info.name);
        for(int pn = 0; pn < info.programCount; ++pn) {
            Vst3Plugin::ProgramInfo info;
            
            Vst::String128 buf = {};
            unit_handler->getProgramName(program_list_id, pn, buf);
            info.name_ = to_wstr(buf);
            
            auto get_attr = [&](auto key) {
                Vst::String128 buf = {};
                unit_handler->getProgramInfo(program_list_id, pn, key, buf);
                return to_wstr(buf);
            };
            
            namespace PA = Vst::PresetAttributes;
            info.plugin_name_ = get_attr(PA::kPlugInName);
            info.plugin_category_ = get_attr(PA::kPlugInCategory);
            info.instrument_ = get_attr(PA::kInstrument);
            info.style_ = get_attr(PA::kStyle);
            info.character_ = get_attr(PA::kCharacter);
            info.state_type_ = get_attr(PA::kStateType);
            info.file_path_string_type_ = get_attr(PA::kFilePathStringType);
            info.file_name_ = get_attr(PA::kFileName);
            pl.programs_.push_back(info);
        }
        break;
    }
    
    return pl;
};

Vst::ParamID FindProgramChangeParam(Vst3Plugin::Impl::ParameterInfoList &list, Vst::UnitID unit_id)
{
    for(auto &entry: list) {
        if(entry.unit_id_ == unit_id && entry.is_program_change_) {
            return entry.id_;
        }
    }
    
    return Vst::kNoParamId;
}

void Vst3Plugin::Impl::PrepareUnitInfo()
{
    if(!unit_handler_) { return; }
    
    size_t const num = unit_handler_->getUnitCount();
    assert(num >= 1); // 少なくとも、unitID = 0のunitは用意されているはず。

    for(size_t i = 0; i < num; ++i) {
        Vst::UnitInfo vui;
        unit_handler_->getUnitInfo(i, vui);
        UnitInfo ui;
        ui.id_ = vui.id;
        ui.name_ = to_wstr(vui.name);
        ui.parent_id_ = vui.parentUnitId;
        if(vui.programListId != Vst::kNoProgramListId) {
            ui.program_list_ = CreateProgramList(unit_handler_.get(), vui.programListId);
            ui.program_change_param_ = FindProgramChangeParam(parameter_info_list_, vui.id);
        }
        
        assert(unit_info_list_.GetIndexByID(ui.id_) == -1); // id should be unique.
        unit_info_list_.AddItem(ui);
    }
    
    if(unit_info_list_.GetIndexByID(Vst::kRootUnitId) == -1) {
        // Root unit is there just as a hierarchy.
        // Add an UnitInfo for the root unit here.
        UnitInfo ui;
        ui.id_ = Vst::kRootUnitId;
        ui.name_ = L"Root";
        ui.parent_id_ = Vst::kNoParentUnitId;
        ui.program_change_param_ = Vst::kNoParamId;
        ui.program_list_.id_ = Vst::kNoProgramListId;
        unit_info_list_.AddItem(ui);
    }
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

	unit_handler_.reset();
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
