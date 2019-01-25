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

#include <pluginterfaces/vst/ivstmidicontrollers.h>

#include "../../misc/StrCnv.hpp"
#include "../../misc/ScopeExit.hpp"
#include "Vst3Utils.hpp"
#include "Vst3Plugin.hpp"
#include "Vst3PluginFactory.hpp"
#include "Vst3Debug.hpp"

using namespace Steinberg;

NS_HWM_BEGIN

extern void* GetWindowRef(NSView *view);

class Error
:    public std::runtime_error
{
public:
    Error(tresult error_code, std::string context)
    :    std::runtime_error("Failed({}){}"_format(tresult_to_string(error_code),
                                                  context.empty() ? "" : ": {}" + context
                                                  ).c_str()
                            )
    {}
};

template<class To>
void ThrowIfNotRight(maybe_vstma_unique_ptr<To> const &ptr, std::string context = "")
{
    if(ptr.is_right() == false) {
        throw Error(ptr.left(), context);
    }
}

void ThrowIfNotFound(tresult result, std::vector<tresult> candidates, std::string context = "")
{
    assert(candidates.size() >= 1);
    
    if(std::find(candidates.begin(), candidates.end(), result) == candidates.end()) {
        throw Error(result, context);
    }
}

void ThrowIfNotOk(tresult result, std::string context = "")
{
    ThrowIfNotFound(result, { kResultOk }, context);
}

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
    
    hwm::dout
    << L"Set speaker arrangement for {} Bus[{}]: {}"_format(dir_ == Vst::BusDirections::kInput ? L"Input" : L"Output",
                                                             bus_index,
                                                             GetSpeakerName(arr)
                                                             )
    << std::endl;
    
    auto input_arrs = owner_->input_buses_info_.GetSpeakers();
    auto output_arrs = owner_->output_buses_info_.GetSpeakers();
    
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

std::vector<Vst::SpeakerArrangement> Vst3Plugin::Impl::AudioBusesInfo::GetSpeakers() const
{
    std::vector<Vst::SpeakerArrangement> arrs;
    size_t const num = GetNumBuses();
    for(size_t i = 0; i < num; ++i) {
        arrs.push_back(GetBusInfo(i).speaker_);
    }
    
    return arrs;
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
                       FactoryInfo const &factory_info,
                       ClassInfo const &class_info,
                       FUnknown *host_context)
	:	factory_info_(factory_info)
    ,   edit_controller_is_created_new_(false)
	,	is_editor_opened_(false)
	,	is_processing_started_(false)
	,	block_size_(2048)
	,	sampling_rate_(44100)
	,	has_editor_(false)
	,	status_(Status::kInvalid)
{
    assert(host_context);
    
	LoadPlugin(factory, class_info, std::move(host_context));

    input_events_.setMaxSize(128);
    output_events_.setMaxSize(128);
}

Vst3Plugin::Impl::~Impl()
{
	UnloadPlugin();
}

FactoryInfo const & Vst3Plugin::Impl::GetFactoryInfo() const
{
    return factory_info_;
}

ClassInfo const & Vst3Plugin::Impl::GetComponentInfo() const
{
    return class_info_;
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
	return class_info_.name();
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

String Vst3Plugin::Impl::ValueToStringByIndex(UInt32 index, ParamValue value)
{
    return ValueToStringByID(GetParameterInfoList().GetItemByIndex(index).id_, value);
}

Vst::ParamValue Vst3Plugin::Impl::StringToValueTByIndex(UInt32 index, String string)
{
    return StringToValueByID(GetParameterInfoList().GetItemByIndex(index).id_, string);
}

String Vst3Plugin::Impl::ValueToStringByID(ParamID id, ParamValue value)
{
    Vst::String128 str = {};
    auto result = edit_controller_->getParamStringByValue(id, value, str);
    if(result != kResultOk) {
        return L"";
    }
    
    return to_wstr(str);
}

Vst::ParamValue Vst3Plugin::Impl::StringToValueByID(ParamID id, String string)
{
    Vst::ParamValue value = 0;
    auto result = edit_controller_->getParamValueByString(id, to_utf16(string).data(), value);
    if(result != kResultOk) {
        return -1;
    }
    
    return value;
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
    
    hwm::dout << "Set Program[{}] of Unit[{}]"_format(index,
                                                      GetUnitInfoList().GetIndexByID(unit_id)
                                                      )
    << std::endl;
    
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
	//assert(is_resumed_);
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

bool operator==(Vst::ProcessSetup const &x, Vst::ProcessSetup const &y)
{
    auto to_tuple = [](auto const &s) {
        return std::tie(s.maxSamplesPerBlock,
                        s.sampleRate,
                        s.symbolicSampleSize,
                        s.processMode);
    };
    
    return to_tuple(x) == to_tuple(y);
}

bool operator!=(Vst::ProcessSetup const &x, Vst::ProcessSetup const &y)
{
    return !(x == y);
}

void Vst3Plugin::Impl::Resume()
{
	assert(status_ == Status::kInitialized || status_ == Status::kSetupDone);

	tresult res;
    
    Vst::ProcessSetup new_setup = {};
    new_setup.maxSamplesPerBlock = block_size_;
    new_setup.sampleRate = sampling_rate_;
    new_setup.symbolicSampleSize = Vst::SymbolicSampleSizes::kSample32;
    new_setup.processMode = Vst::ProcessModes::kRealtime;
    
    if(new_setup != applied_process_setup_) {
        res = GetAudioProcessor()->setupProcessing(new_setup);
        if(res != kResultOk && res != kNotImplemented) {
            throw Error(res, "setupProcessing failed");
        } else {
            applied_process_setup_ = new_setup;
        }
    }

    status_ = Status::kSetupDone;
    
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
	if(res != kResultOk && res != kNotImplemented) {
        throw Error(res, "setActive failed");
    }
    
	status_ = Status::kActivated;
		
	hwm::dout << "Latency samples : " << GetAudioProcessor()->getLatencySamples() << std::endl;

	//! doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioProcessor.html#af252fd721b195b793f3a5dfffc069401
	/*!
		@memo
		Informs the Plug-in about the processing state.
		This will be called before process calls (one or more) start with true and after with false.
		In this call the Plug-in should do only light operation (no memory allocation or big setup reconfiguration), this could be used to reset some buffers (like Delay line or Reverb).
	*/
	res = GetAudioProcessor()->setProcessing(true);
    ThrowIfNotFound(res, { kResultOk, kNotImplemented });

    status_ = Status::kProcessing;
	is_processing_started_ = true;
}

void Vst3Plugin::Impl::Suspend()
{
	if(status_ == Status::kProcessing) {
		GetAudioProcessor()->setProcessing(false);
		status_ = Status::kActivated;
	}


	GetComponent()->setActive(false);
	status_ = Status::kSetupDone;
}

bool Vst3Plugin::Impl::IsResumed() const
{
    return (int)status_ > (int)Status::kSetupDone;
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

        tresult res;
        Steinberg::MemoryStream stream;
		//! ReloadComponentで行う内容はこれで合っているかどうか分からない。
		res = component_->getState(&stream);
        ShowError(res, L"getState");

		bool const is_resumed = IsResumed();
		if(is_resumed) {
			Suspend();
			Resume();
		}

		res = component_->setState(&stream);
        ShowError(res, L"setState");
	}
}

void Vst3Plugin::Impl::Process(ProcessInfo pi)
{
    assert(pi.time_info_);
    auto &ti = *pi.time_info_;
	Vst::ProcessContext process_context = {};
	process_context.sampleRate = sampling_rate_;
	process_context.projectTimeSamples = ti.smp_begin_pos_;
    process_context.projectTimeMusic = ti.ppq_begin_pos_;
	process_context.tempo = ti.tempo_;
	process_context.timeSigDenominator = ti.time_sig_denom_;
	process_context.timeSigNumerator = ti.time_sig_numer_;

    using Flags = Vst::ProcessContext::StatesAndFlags;
    
	process_context.state
    = (ti.playing_ ? Flags::kPlaying : 0)
    | Flags::kProjectTimeMusicValid
    | Flags::kTempoValid
    | Flags::kTimeSigValid
    ;

    input_events_.clear();
    output_events_.clear();
    input_params_.clearQueue();
    output_params_.clearQueue();
    input_buffer_.fill();
    output_buffer_.fill();
    
    for(auto &m: pi.input_midi_buffer_.buffer_) {
        Vst::Event e;
        e.busIndex = 0;
        e.sampleOffset = m.offset_;
        e.ppqPosition = process_context.projectTimeMusic;
        e.flags = Vst::Event::kIsLive;
        
        using namespace MidiDataType;
        
        auto midi_map = [this](int channel, int offset, int cc, Vst::ParamValue value) {
            if(!midi_mapping_) { return; }
            Vst::ParamID param_id = 0;
            auto result = midi_mapping_->getMidiControllerAssignment(0, channel, cc, param_id);
            if(result == kResultOk) {
                PushBackParameterChange(param_id, value, offset);
                edit_controller_->setParamNormalized(param_id, value);
            }
        };
        
        if(auto note_on = m.As<NoteOn>()) {
            e.type = Vst::Event::kNoteOnEvent;
            e.noteOn.channel = m.channel_;
            e.noteOn.pitch = note_on->pitch_;
            e.noteOn.velocity = note_on->velocity_ / 127.0;
            e.noteOn.length = 0;
            e.noteOn.tuning = 0;
            e.noteOn.noteId = -1;
            input_events_.addEvent(e);
        } else if(auto note_off = m.As<NoteOff>()){
            e.type = Vst::Event::kNoteOffEvent;
            e.noteOff.channel = m.channel_;
            e.noteOff.pitch = note_off->pitch_;
            e.noteOff.velocity = note_off->off_velocity_ / 127.0;
            e.noteOff.tuning = 0;
            e.noteOff.noteId = -1;
            input_events_.addEvent(e);
        } else if(auto poly_press = m.As<PolyphonicKeyPressure>()) {
            e.type = Vst::Event::kPolyPressureEvent;
            e.polyPressure.channel = m.channel_;
            e.polyPressure.pitch = poly_press->pitch_;
            e.polyPressure.pressure = poly_press->value_ / 127.0;
            e.polyPressure.noteId = -1;
            input_events_.addEvent(e);
        } else if(auto cc = m.As<ControlChange>()) {
            midi_map(m.channel_, m.offset_, cc->control_number_, cc->data_ / 128.0);
        } else if(auto cp = m.As<ChannelPressure>()) {
            midi_map(m.channel_, m.offset_, Vst::ControllerNumbers::kAfterTouch, cp->value_ / 128.0);
        } else if(auto pb = m.As<PitchBendChange>()) {
            midi_map(m.channel_, m.offset_, Vst::ControllerNumbers::kPitchBend,
                     ((pb->value_msb_ << 7) | pb->value_lsb_) / 16384.0);
        }
    }
	
    auto copy_buffer = [&](BufferRef<const float> src, BufferRef<float> dest,
                           SampleCount length_to_copy)
    {
        size_t const min_ch = std::min(src.channels(), dest.channels());
        if(min_ch == 0) { return; }

        assert(src.samples() >= length_to_copy);
        assert(dest.samples() >= length_to_copy);
        
        for(size_t ch = 0; ch < min_ch; ++ch) {
            std::copy_n(src.get_channel_data(ch),
                        length_to_copy,
                        dest.get_channel_data(ch));
        }
    };
    
    copy_buffer(pi.input_audio_buffer_, input_buffer_,
                pi.time_info_->GetSmpDuration()
                );

	PopFrontParameterChanges(input_params_);

	Vst::ProcessData process_data;
	process_data.processContext = &process_context;
	process_data.processMode = Vst::ProcessModes::kRealtime;
	process_data.symbolicSampleSize = Vst::SymbolicSampleSizes::kSample32;
    process_data.numSamples = ti.GetSmpDuration();
    process_data.numInputs = input_buses_info_.GetNumBuses();
	process_data.numOutputs = output_buses_info_.GetNumBuses();
    process_data.inputs = input_buses_info_.GetBusBuffers();
	process_data.outputs = output_buses_info_.GetBusBuffers();
	process_data.inputEvents = &input_events_;
	process_data.outputEvents = &output_events_;
	process_data.inputParameterChanges = &input_params_;
	process_data.outputParameterChanges = &output_params_;

	auto const res = GetAudioProcessor()->process(process_data);
    if(res != kResultOk) {
        hwm::dout << "process failed: {}"_format(tresult_to_string(res)) << std::endl;
    }
    
    copy_buffer(output_buffer_, pi.output_audio_buffer_,
                pi.time_info_->GetSmpDuration()
                );

	for(int i = 0; i < output_params_.getParameterCount(); ++i) {
		auto *queue = output_params_.getParameterData(i);
		if(queue && queue->getPointCount() > 0) {
            hwm::dout << "Output parameter count [{}] : {}"_format(i, queue->getPointCount()) << std::endl;
		}
	}
}

void Vst3Plugin::Impl::PushBackParameterChange(Vst::ParamID id, Vst::ParamValue value, SampleCount offset)
{
	auto lock = std::unique_lock(parameter_queue_mutex_);
	Steinberg::int32 parameter_index = 0;
	auto *queue = param_changes_queue_.addParameterData(id, parameter_index);
	Steinberg::int32 ref_point_index = 0;
	queue->addPoint(offset, value, ref_point_index);
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
    auto cid = FUID::fromTUID(info.cid().data());
	auto maybe_component = createInstance<Vst::IComponent>(factory, cid);
    ThrowIfNotRight(maybe_component);
    
	auto component = std::move(maybe_component.right());
	tresult res;
	res = component->setIoMode(Vst::IoModes::kAdvanced);
    ThrowIfNotFound(res, { kResultOk, kNotImplemented });
    
	status_ = Status::kCreated;

	res = component->initialize(host_context);
    ThrowIfNotOk(res);
    
	status_ = Status::kInitialized;

    HWM_SCOPE_EXIT([&component] {
		if(component) {
			component->terminate();
			component.reset();
		}
    });

	auto maybe_audio_processor = queryInterface<Vst::IAudioProcessor>(component);
    ThrowIfNotRight(maybe_audio_processor);
    
	auto audio_processor = std::move(maybe_audio_processor.right());

	res = audio_processor->canProcessSampleSize(Vst::SymbolicSampleSizes::kSample32);
    ThrowIfNotOk(res);

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
		}
	}

    ThrowIfNotRight(maybe_edit_controller);
    
	edit_controller_ptr_t edit_controller = std::move(maybe_edit_controller.right());

	if(edit_controller_is_created_new) {
		res = edit_controller->initialize(host_context);
        ThrowIfNotOk(res);
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
    
    auto maybe_midi_mapping = queryInterface<Vst::IMidiMapping>(edit_controller);

	class_info_ = info;
	component_ = std::move(component);
	audio_processor_ = std::move(audio_processor);
	edit_controller_ = std::move(edit_controller);
	edit_controller2_ = std::move(edit_controller2);
	edit_controller_is_created_new_ = edit_controller_is_created_new;
    if(maybe_midi_mapping.is_right()) {
        midi_mapping_ = std::move(maybe_midi_mapping.right());
    }
}

void Vst3Plugin::Impl::Initialize(vstma_unique_ptr<Vst::IComponentHandler> component_handler)
{
	tresult res;

	if(edit_controller_) {	
		if(component_handler) {
			res = edit_controller_->setComponentHandler(component_handler.get());
            ThrowIfNotOk(res);
		}

		auto maybe_cpoint_component = queryInterface<Vst::IConnectionPoint>(component_);
		auto maybe_cpoint_edit_controller = queryInterface<Vst::IConnectionPoint>(edit_controller_);

        if(maybe_cpoint_component.is_right() && maybe_cpoint_edit_controller.is_right()) {
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
                hwm::dout << "Warning: This plugin has no unit info." << std::endl;
                // Treat as this plugin has no IUnitInfo
                unit_handler_.reset();
            }
        }
        
        if(!unit_handler_) {
            hwm::dout << "This Plugin has no IUnitInfo interfaces." << std::endl;
        }

        OutputBusInfo(component_.get(), edit_controller_.get(), unit_handler_.get());

        input_buses_info_.Initialize(this, Vst::BusDirections::kInput);
        output_buses_info_.Initialize(this, Vst::BusDirections::kOutput);
        
        for(int i = 0; i < input_buses_info_.GetNumBuses(); ++i) {
            input_buses_info_.SetActive(i);
        }
        
        for(int i = 0; i < output_buses_info_.GetNumBuses(); ++i) {
            output_buses_info_.SetActive(i);
        }
       
        auto input_speakers = input_buses_info_.GetSpeakers();
        auto output_speakers = output_buses_info_.GetSpeakers();

        auto const result = audio_processor_->setBusArrangements(input_speakers.data(), input_speakers.size(),
                                                                 output_speakers.data(), output_speakers.size());
        if(result != kResultOk) {
            hwm::dout << "Failed to set bus arrangement: " << tresult_to_string(result) << std::endl;
        }

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
    assert(edit_controller_);

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
