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
#include <thread>

#include <pluginterfaces/vst/ivstmidicontrollers.h>

#include "../../misc/StrCnv.hpp"
#include "../../misc/ScopeExit.hpp"
#include "Vst3Utils.hpp"
#include "Vst3Plugin.hpp"
#include "Vst3PluginFactory.hpp"
#include "Vst3Debug.hpp"

using namespace Steinberg;

NS_HWM_BEGIN

#if !defined(_MSC_VER)
extern void* GetWindowRef(NSView *view);
#endif

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

std::vector<Vst3Plugin::BusInfo> CreateBusInfoList(Vst::IComponent *comp,
                                                   Vst::MediaTypes media,
                                                   Vst::BusDirections dir)
{
    std::vector<Vst3Plugin::BusInfo> bus_infos;
    size_t const num_buses = comp->getBusCount(media, dir);
    bus_infos.resize(num_buses);
    
    tresult ret = kResultTrue;
    for(size_t i = 0; i < num_buses; ++i) {
        Vst::BusInfo vbi;
        ret = comp->getBusInfo(media, dir, i, vbi);
        if(ret != kResultTrue) { throw std::runtime_error("Failed to get BusInfo"); }
        
        Vst3Plugin::BusInfo bi;
        bi.bus_type_ = static_cast<Vst::BusTypes>(vbi.busType);
        bi.channel_count_ = vbi.channelCount;
        bi.direction_ = static_cast<Vst::BusDirections>(vbi.direction);
        bi.is_default_active_ = (vbi.flags & Vst::BusInfo::kDefaultActive) != 0;
        bi.media_type_ = static_cast<Vst::MediaTypes>(vbi.mediaType);
        bi.name_ = to_wstr(vbi.name);
        bi.is_active_ = bi.is_default_active_;
        
        bus_infos[i] = bi;
    }
    
    return bus_infos;
}

void Vst3Plugin::Impl::MidiBusesInfo::Initialize(Impl *owner, Vst::BusDirections dir)
{
    owner_ = owner;
    dir_ = dir;
    
    auto const media = Vst::MediaTypes::kEvent;
    auto bus_infos = CreateBusInfoList(owner_->component_.get(), media, dir);
    
    bus_infos_ = std::move(bus_infos);
}

size_t Vst3Plugin::Impl::MidiBusesInfo::GetNumBuses() const
{
    return bus_infos_.size();
}

Vst3Plugin::BusInfo const &
Vst3Plugin::Impl::MidiBusesInfo::GetBusInfo(UInt32 bus_index) const
{
    assert(bus_index < GetNumBuses());
    return bus_infos_[bus_index];
}

bool Vst3Plugin::Impl::MidiBusesInfo::IsActive(size_t bus_index) const
{
    assert(bus_index < GetNumBuses());
    return bus_infos_[bus_index].is_active_;
}

void Vst3Plugin::Impl::MidiBusesInfo::SetActive(size_t bus_index, bool state)
{
    assert(bus_index < GetNumBuses());
    
    auto &comp = owner_->component_;
    auto const result = comp->activateBus(Vst::MediaTypes::kEvent, dir_, bus_index, state);
    if(result != kResultTrue) {
        throw std::runtime_error("Failed to activate a bus");
    }
    
    bus_infos_[bus_index].is_active_ = state;
    SetupActiveBusTable();
}

UInt32 Vst3Plugin::Impl::MidiBusesInfo::GetNumActiveBuses() const
{
    return active_bus_index_to_bus_index_.size();
}

UInt32 Vst3Plugin::Impl::MidiBusesInfo::GetBusIndexFromActiveBusIndex(UInt32 active_bus_index) const
{
    auto found = active_bus_index_to_bus_index_.find(active_bus_index);
    if(found == active_bus_index_to_bus_index_.end()) { return -1; }
    
    return found->second;
}

UInt32 Vst3Plugin::Impl::MidiBusesInfo::GetActiveBusIndexFromBusIndex(UInt32 bus_index) const
{
    auto found = bus_index_to_active_bus_index_.find(bus_index);
    if(found == bus_index_to_active_bus_index_.end()) { return -1; }
    
    return found->second;
}

void Vst3Plugin::Impl::MidiBusesInfo::SetupActiveBusTable()
{
    bus_index_to_active_bus_index_.clear();
    active_bus_index_to_bus_index_.clear();
    
    for(int i = 0; i < bus_infos_.size(); ++i) {
        if(bus_infos_[i].is_active_) {
            auto bus_index = i;
            auto active_bus_index = active_bus_index_to_bus_index_.size();
            
            bus_index_to_active_bus_index_.emplace(bus_index, active_bus_index);
            active_bus_index_to_bus_index_.emplace(active_bus_index, bus_index);
        }
    }
}

void Vst3Plugin::Impl::AudioBusesInfo::Initialize(Impl *owner, Vst::BusDirections dir)
{
    owner_ = owner;
    dir_ = dir;
    
    auto const media = Vst::MediaTypes::kAudio;
    auto bus_infos = CreateBusInfoList(owner_->component_.get(), media, dir);
    
    for(size_t i = 0; i < bus_infos.size(); ++i) {
        Vst::SpeakerArrangement arr;
        auto ret = owner_->audio_processor_->getBusArrangement(dir, i, arr);
        if(ret != kResultTrue) { throw std::runtime_error("Failed to get SpeakerArrangement"); }
        bus_infos[i].speaker_ = arr;
    }
    
    bus_infos_ = std::move(bus_infos);
    
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
    
    hwm::wdout
    << L"Set speaker arrangement for {} Bus[{}]: {}"_format(dir_ == Vst::BusDirections::kInput ? L"Input" : L"Output",
                                                             bus_index,
                                                             GetSpeakerName(arr)
                                                             )
    << std::endl;
    
    auto input_arrs = owner_->input_audio_buses_info_.GetSpeakers();
    auto output_arrs = owner_->output_audio_buses_info_.GetSpeakers();
    
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
    ,   is_single_component_(false)
	,	is_editor_opened_(false)
	,	block_size_(2048)
	,	sampling_rate_(44100)
	,	has_editor_(false)
	,	status_(Status::kInvalid)
{
    assert(host_context);
    
    LoadInterfaces(factory, class_info, host_context);
    Initialize();

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

Vst3Plugin::Impl::AudioBusesInfo & Vst3Plugin::Impl::GetAudioBusesInfo(Vst::BusDirections dir)
{
    if(dir == Vst::BusDirections::kInput) {
        return input_audio_buses_info_;
    } else {
        return output_audio_buses_info_;
    }
}

Vst3Plugin::Impl::AudioBusesInfo const & Vst3Plugin::Impl::GetAudioBusesInfo(Vst::BusDirections dir) const
{
    if(dir == Vst::BusDirections::kInput) {
        return input_audio_buses_info_;
    } else {
        return output_audio_buses_info_;
    }
}

Vst3Plugin::Impl::MidiBusesInfo & Vst3Plugin::Impl::GetMidiBusesInfo(Vst::BusDirections dir)
{
    if(dir == Vst::BusDirections::kInput) {
        return input_midi_buses_info_;
    } else {
        return output_midi_buses_info_;
    }
}

Vst3Plugin::Impl::MidiBusesInfo const & Vst3Plugin::Impl::GetMidiBusesInfo(Vst::BusDirections dir) const
{
    if(dir == Vst::BusDirections::kInput) {
        return input_midi_buses_info_;
    } else {
        return output_midi_buses_info_;
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

void Vst3Plugin::Impl::SetParameterValueByIndex(UInt32 index, Vst::ParamValue value)
{
    auto id = GetParameterInfoList().GetItemByIndex(index).id_;
    return SetParameterValueByID(id, value);
}

void Vst3Plugin::Impl::SetParameterValueByID(Vst::ParamID id, Vst::ParamValue value)
{
    edit_controller_->setParamNormalized(id, value);
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

auto to_vst_utf16(String const &str) {
#if defined(_MSC_VER)
	return to_wstr(str);
#else
	return to_utf16(str);
#endif
}

Vst::ParamValue Vst3Plugin::Impl::StringToValueByID(ParamID id, String string)
{
    Vst::ParamValue value = 0;
    auto result = edit_controller_->getParamValueByString(id, to_vst_utf16(string).data(), value);
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
    
    auto param_info = GetParameterInfoList().FindItemByID(param_id);
    if(!param_info) {
        return -1;
    }
    
    auto const normalized = GetEditController()->getParamNormalized(param_id);
    
    auto const plain = std::min<UInt32>(param_info->step_count_,
                                        normalized * (param_info->step_count_ + 1)
                                        );
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
    
    auto param_info = GetParameterInfoList().FindItemByID(param_id);
    if(!param_info) {
        return;
    }
    
    auto const normalized_value = index / (double)param_info->step_count_;
    
    GetEditController()->setParamNormalized(unit_info.program_change_param_, normalized_value);
    PushBackParameterChange(unit_info.program_change_param_, normalized_value);
}

bool Vst3Plugin::Impl::HasEditor() const
{
	assert(component_);
	//assert(is_resumed_);
	return has_editor_.get();
}

void Vst3Plugin::Impl::CheckHavingEditor()
{
    // some plugin (e.g., TyrellN6) may crash while loading saved data
    // if a plug view has been created but not attached to an window handle.
    auto res = CreatePlugView();
    has_editor_ = (res == kResultOk);
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
    return (bool)is_editor_opened_;
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
    
    prepare_bus_buffers(input_audio_buses_info_, block_size_, input_buffer_);
    prepare_bus_buffers(output_audio_buses_info_, block_size_, output_buffer_);

	res = GetComponent()->setActive(true);
	if(res != kResultOk && res != kNotImplemented) {
        throw Error(res, "setActive failed");
    }
    
	status_ = Status::kActivated;
		
	hwm::dout << "Latency samples : " << GetAudioProcessor()->getLatencySamples() << std::endl;

	auto lock = lf_processing_.make_lock(std::try_to_lock);
    
	res = GetAudioProcessor()->setProcessing(true);
    ThrowIfNotFound(res, { kResultOk, kNotImplemented });

    status_ = Status::kProcessing;
}

void Vst3Plugin::Impl::Suspend()
{
    if(IsResumed() == false) { return; }
    
    for( ; ; ) {
        auto lock = lf_processing_.make_lock(std::try_to_lock);
        if(!lock) {
            std::this_thread::yield();
            continue;
        }
        
        if(status_ == Status::kProcessing) {
            GetAudioProcessor()->setProcessing(false);
            status_ = Status::kActivated;
        }
        
        break;
    }

	GetComponent()->setActive(false);
	status_ = Status::kSetupDone;
}

bool Vst3Plugin::Impl::IsResumed() const
{
    return (int)status_.load() > (int)Status::kSetupDone;
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
        hwm::dout << "Param values changed" << std::endl;
        auto const num = GetNumParameters();
        for(int i = 0; i < num; ++i) {
            auto const value = GetParameterValueByIndex(i);
            auto const &info = GetParameterInfoList().GetItemByIndex(i);
            PushBackParameterChange(info.id_, value);
        }
    } else if((flags & Vst::RestartFlags::kIoChanged)) {
        hwm::dout << "IO changed" << std::endl;
	} else if((flags & Vst::RestartFlags::kReloadComponent)) {
		hwm::dout << "Should reload component" << std::endl;
        bool const is_resumed = IsResumed();
        Suspend();
        
        component_->setActive(false);
        component_->setActive(true);
        
        if(is_resumed) {
            Resume();
        }
	}
}

std::optional<ProcessInfo::MidiMessage> ToProcessEvent(Vst::Event const &ev)
{
    ProcessInfo::MidiMessage msg;
    msg.offset_ = ev.sampleOffset;
    msg.ppq_pos_ = ev.ppqPosition;
    
    using namespace MidiDataType;
    if(ev.type == Vst::Event::kNoteOnEvent) {
        NoteOn on;
        on.pitch_ = ev.noteOn.pitch;
        on.velocity_ = std::min<int>(127, (int)(ev.noteOn.velocity * 128.0));
        msg.channel_ = ev.noteOn.channel;
        msg.data_ = on;
    } else if(ev.type == Vst::Event::kNoteOffEvent) {
        NoteOff off;
        off.pitch_ = ev.noteOff.pitch;
        off.off_velocity_ = std::min<int>(127, (int)(ev.noteOff.velocity * 128.0));
        msg.channel_ = ev.noteOff.channel;
        msg.data_ = off;
    } else if(ev.type == Vst::Event::kPolyPressureEvent) {
        PolyphonicKeyPressure pre;
        pre.pitch_ = ev.polyPressure.pitch;
        pre.value_ = std::min<int>(127, (int)(ev.polyPressure.pressure * 128.0));
        msg.channel_ = ev.polyPressure.channel;
        msg.data_ = pre;
    } else if(ev.type == Vst::Event::kDataEvent) {
        hwm::dout << "Plugin sends data events." << std::endl;
        return std::nullopt;
    } else if(ev.type == Vst::Event::kChordEvent) {
        hwm::dout << "Plugin sends chord events." << std::endl;
        return std::nullopt;
    } else if(ev.type == Vst::Event::kScaleEvent) {
        hwm::dout << "Plugin sends scake events." << std::endl;
        return std::nullopt;
    }
    
    return msg;
}

std::optional<Vst::Event> const ToVstEvent(ProcessInfo::MidiMessage const &msg)
{
    Vst::Event e;
    e.busIndex = 0;
    e.sampleOffset = msg.offset_;
    e.ppqPosition = 0;
    e.flags = Vst::Event::kIsLive;
    
    using namespace MidiDataType;
    
    if(auto note_on = msg.As<NoteOn>()) {
        hwm::dout << "Input Note On Event ch:{}, pi:{}, vel{}"_format(msg.channel_,
                                                                      note_on->pitch_,
                                                                      note_on->velocity_) << std::endl;
        e.type = Vst::Event::kNoteOnEvent;
        e.noteOn.channel = msg.channel_;
        e.noteOn.pitch = note_on->pitch_;
        e.noteOn.velocity = note_on->velocity_ / 127.0;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0;
        e.noteOn.noteId = -1;
        return e;
    } else if(auto note_off = msg.As<NoteOff>()){
        hwm::dout << "Input Note Off Event ch:{}, pi:{}, vel{}"_format(msg.channel_,
                                                                      note_off->pitch_,
                                                                      note_off->off_velocity_) << std::endl;
        e.type = Vst::Event::kNoteOffEvent;
        e.noteOff.channel = msg.channel_;
        e.noteOff.pitch = note_off->pitch_;
        e.noteOff.velocity = note_off->off_velocity_ / 127.0;
        e.noteOff.tuning = 0;
        e.noteOff.noteId = -1;
        return e;
    } else if(auto poly_press = msg.As<PolyphonicKeyPressure>()) {
        e.type = Vst::Event::kPolyPressureEvent;
        e.polyPressure.channel = msg.channel_;
        e.polyPressure.pitch = poly_press->pitch_;
        e.polyPressure.pressure = poly_press->value_ / 127.0;
        e.polyPressure.noteId = -1;
        return e;
    } else {
        return std::nullopt;
    }
}

void Vst3Plugin::Impl::InputEvents(ProcessInfo::IEventBufferList const *buffers,
                 Vst::ProcessContext const &process_context)
{
    auto const num_active_buses = input_midi_buses_info_.GetNumActiveBuses();
    auto num_buffers = buffers->GetNumBuffers();
    for(int bi = 0; bi < num_buffers; ++bi) {
        auto buf = buffers->GetBuffer(bi);
        if(bi >= num_active_buses) { break; }
        
        UInt32 const bus_index = input_midi_buses_info_.GetBusIndexFromActiveBusIndex(bi);
        
        for(auto &m: buf->GetRef()) {
            using namespace MidiDataType;
    
            auto midi_map = [this](int channel, int offset, int cc, Vst::ParamValue value) {
                if(!midi_mapping_) { return; }
                Vst::ParamID param_id = 0;
                auto result = midi_mapping_->getMidiControllerAssignment(0, channel, cc, param_id);
                if(result == kResultOk) {
                    PushBackParameterChange(param_id, value, offset);
                }
            };
            
            auto vst_event = ToVstEvent(m);
            if(vst_event) {
                vst_event->busIndex = bus_index;
                vst_event->ppqPosition = process_context.projectTimeMusic;
                input_events_.addEvent(*vst_event);
            } else if(auto cc = m.As<ControlChange>()) {
                midi_map(m.channel_, m.offset_, cc->control_number_, cc->data_ / 128.0);
            } else if(auto cp = m.As<ChannelPressure>()) {
                midi_map(m.channel_, m.offset_, Vst::ControllerNumbers::kAfterTouch, cp->value_ / 128.0);
            } else if(auto pb = m.As<PitchBendChange>()) {
                midi_map(m.channel_, m.offset_, Vst::ControllerNumbers::kPitchBend,
                         ((pb->value_msb_ << 7) | pb->value_lsb_) / 16384.0);
            }
        }
    }
}

void Vst3Plugin::Impl::OutputEvents(ProcessInfo::IEventBufferList *buffers,
                                    Vst::ProcessContext const &process_context)
{
    auto const num_buffers = buffers->GetNumBuffers();
    
    auto const num_events = output_events_.getEventCount();
    for(int ei = 0; ei < num_events; ++ei) {
        Vst::Event ev;
        auto result = output_events_.getEvent(ei, ev);
        assert(result == kResultOk);
        
        auto bi = output_midi_buses_info_.GetActiveBusIndexFromBusIndex(ev.busIndex);
        if(bi >= num_buffers) { continue; }
        auto buf = buffers->GetBuffer(bi);
        
        auto msg = ToProcessEvent(ev);
        if(msg) {
            buf->AddEvent(*msg);
        }
    }
}

void Vst3Plugin::Impl::Process(ProcessInfo pi)
{
    auto lock = lf_processing_.make_lock();
    
    if(status_ != Status::kProcessing) { return; }
    
    assert(pi.time_info_);
    auto &ti = *pi.time_info_;
	Vst::ProcessContext process_context = {};
	process_context.sampleRate = sampling_rate_;
	process_context.projectTimeSamples = ti.play_.begin_.sample_;
    process_context.projectTimeMusic = ti.play_.begin_.tick_ / ti.tpqn_;
	process_context.tempo = ti.tempo_;
	process_context.timeSigDenominator = ti.meter_.denom_;
	process_context.timeSigNumerator = ti.meter_.numer_;
    SampleCount sample_length = ti.play_.duration_.sample_;

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
    
    InputEvents(pi.input_event_buffers_, process_context);
	
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
                sample_length
                );

	PopFrontParameterChanges(input_params_);

	Vst::ProcessData process_data;
	process_data.processContext = &process_context;
	process_data.processMode = Vst::ProcessModes::kRealtime;
	process_data.symbolicSampleSize = Vst::SymbolicSampleSizes::kSample32;
    process_data.numSamples = sample_length;
    process_data.numInputs = input_audio_buses_info_.GetNumBuses();
	process_data.numOutputs = output_audio_buses_info_.GetNumBuses();
    process_data.inputs = input_audio_buses_info_.GetBusBuffers();
	process_data.outputs = output_audio_buses_info_.GetBusBuffers();
	process_data.inputEvents = &input_events_;
	process_data.outputEvents = &output_events_;
	process_data.inputParameterChanges = &input_params_;
	process_data.outputParameterChanges = &output_params_;

	auto const res = GetAudioProcessor()->process(process_data);
    
    OutputEvents(pi.output_event_buffers_, process_context);
    
    static bool kOutputParameter = false;
    
    if(res != kResultOk) {
        hwm::dout << "process failed: {}"_format(tresult_to_string(res)) << std::endl;
    }
    
    copy_buffer(output_buffer_, pi.output_audio_buffer_,
                sample_length
                );

	for(int i = 0; i < output_params_.getParameterCount(); ++i) {
		auto *queue = output_params_.getParameterData(i);
		if(queue && queue->getPointCount() > 0 && kOutputParameter) {
            hwm::dout << "Output parameter count [{}] : {}"_format(i, queue->getPointCount()) << std::endl;
		}
	}
}

void Vst3Plugin::Impl::PushBackParameterChange(Vst::ParamID id, Vst::ParamValue value, SampleCount offset)
{
	auto lock = lf_parameter_queue_.make_lock();
    
	Steinberg::int32 parameter_index = 0;
	auto *queue = param_changes_queue_.addParameterData(id, parameter_index);
	Steinberg::int32 ref_point_index = 0;
	queue->addPoint(offset, value, ref_point_index);
    (void)ref_point_index; // currently unused.
}

void Vst3Plugin::Impl::PopFrontParameterChanges(Vst::ParameterChanges &dest)
{
    auto lock = lf_parameter_queue_.make_lock();

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

void Vst3Plugin::Impl::LoadInterfaces(IPluginFactory *factory, ClassInfo const &info, FUnknown *host_context)
{
    assert(status_ == Status::kInvalid);
    
    auto cid = FUID::fromTUID(info.cid().data());
	auto component = createInstance<Vst::IComponent>(factory, cid);
    ThrowIfNotRight(component);
    
	tresult res;
	res = component.right()->setIoMode(Vst::IoModes::kAdvanced);
    ThrowIfNotFound(res, { kResultOk, kNotImplemented });
    
    res = component.right()->initialize(host_context);
    ThrowIfNotOk(res);
    
    HWM_SCOPE_EXIT([&, this] {
        if(status_ != Status::kCreated) {
            component.right()->terminate();
        }
    });
    
	// $(DOCUMENT_ROOT)/vstsdk360_22_11_2013_build_100/VST3%20SDK/doc/vstinterfaces/index.html
	// Although it is not recommended, it is possible to implement both, 
	// the processing part and the controller part in one component class.
	// The host tries to query the Steinberg::Vst::IEditController 
	// interface after creating an Steinberg::Vst::IAudioProcessor 
	// and on success uses it as controller. 
	auto edit_controller = queryInterface<Vst::IEditController>(component.right());
	bool is_single_component = false;
    if(edit_controller) {
        is_single_component = true;
    } else {
		TUID controller_id;
		component.right()->getControllerClassId(controller_id);
        //ThrowIfNotOk(res);
        
        edit_controller = createInstance<Vst::IEditController>(factory, FUID::fromTUID(controller_id));
        
        //! Not right if this plugin has no edit controller. Such a plugin is not supported for Terra.
        ThrowIfNotRight(edit_controller);
	}

    assert(edit_controller.is_right());
    
    if(is_single_component == false) {
        res = edit_controller.right()->initialize(host_context);
        ThrowIfNotOk(res);
    }
    
    HWM_SCOPE_EXIT([&, this] {
        if(status_ != Status::kCreated && is_single_component == false) {
            edit_controller.right()->terminate();
        }
    });
    
    auto component_handler = queryInterface<Vst::IComponentHandler>(host_context);
    assert(component_handler.is_right());
    res = edit_controller.right()->setComponentHandler(component_handler.right().get());
    ThrowIfNotOk(res);
    
    auto audio_processor = queryInterface<Vst::IAudioProcessor>(component.right());
    ThrowIfNotRight(audio_processor);

	auto edit_controller2 = queryInterface<Vst::IEditController2>(edit_controller.right());
    if(edit_controller2) {
        hwm::dout << "This pluging implements IEditController2 interface." << std::endl;
    }
    
    auto midi_mapping = queryInterface<Vst::IMidiMapping>(edit_controller.right());
    auto unit_info = queryInterface<Vst::IUnitInfo>(edit_controller.right());
    
	class_info_ = info;
	component_ = std::move(component.right());
	audio_processor_ = std::move(audio_processor.right());
	edit_controller_ = std::move(edit_controller.right());
    is_single_component_ = is_single_component;
    
    if(edit_controller2) {
        edit_controller2_ = std::move(edit_controller2.right());
    }
    
    if(midi_mapping) {
        midi_mapping_ = std::move(midi_mapping.right());
    }
    
    if(unit_info) {
        unit_handler_ = std::move(unit_info.right());
    }
    status_ = Status::kCreated;
}

void Vst3Plugin::Impl::Initialize()
{
    tresult res = kResultFalse;
    
    assert(component_);
    assert(edit_controller_);
    
    res = audio_processor_->canProcessSampleSize(Vst::SymbolicSampleSizes::kSample32);
    ThrowIfNotOk(res);
	
    auto cp_comp = queryInterface<Vst::IConnectionPoint>(component_);
    auto cp_edit = queryInterface<Vst::IConnectionPoint>(edit_controller_);

    if(cp_comp && cp_edit) {
        cp_comp.right()->connect(cp_edit.right().get());
        cp_edit.right()->connect(cp_comp.right().get());
    }
    
    OutputParameterInfo(edit_controller_.get());
    
    if(unit_handler_) {
        OutputUnitInfo(unit_handler_.get());
        
        if(unit_handler_->getUnitCount() == 0) {
            hwm::dout << "Warning: This plugin has no unit info." << std::endl;
            // Treat as this plugin has no IUnitInfo
            unit_handler_.reset();
        }
    } else {
        hwm::dout << "This Plugin has no IUnitInfo interfaces." << std::endl;
    }

    OutputBusInfo(component_.get(), edit_controller_.get(), unit_handler_.get());

    input_audio_buses_info_.Initialize(this, Vst::BusDirections::kInput);
    output_audio_buses_info_.Initialize(this, Vst::BusDirections::kOutput);
    
    for(int i = 0; i < input_audio_buses_info_.GetNumBuses(); ++i) {
        input_audio_buses_info_.SetActive(i);
    }
    
    for(int i = 0; i < output_audio_buses_info_.GetNumBuses(); ++i) {
        output_audio_buses_info_.SetActive(i);
    }
   
    auto input_speakers = input_audio_buses_info_.GetSpeakers();
    auto output_speakers = output_audio_buses_info_.GetSpeakers();

    auto const result = audio_processor_->setBusArrangements(input_speakers.data(), input_speakers.size(),
                                                             output_speakers.data(), output_speakers.size());
    if(result != kResultOk) {
        hwm::dout << "Failed to set bus arrangement: " << tresult_to_string(result) << std::endl;
    }
    
    input_midi_buses_info_.Initialize(this, Vst::BusDirections::kInput);
    output_midi_buses_info_.Initialize(this, Vst::BusDirections::kOutput);

    PrepareParameters();
    PrepareUnitInfo();
    
    // synchronize controller to component by using setComponentState
    MemoryStream stream;
    if (component_->getState (&stream) == kResultOk)
    {
        stream.seek(0, Steinberg::IBStream::IStreamSeekMode::kIBSeekSet, 0);
        edit_controller_->setComponentState (&stream);
        
    }

    input_params_.setMaxParameters(parameter_info_list_.size());
    output_params_.setMaxParameters(parameter_info_list_.size());
    
    status_ = Status::kInitialized;
}

tresult Vst3Plugin::Impl::CreatePlugView()
{
    assert(edit_controller_);

    // do nothing if already created.
    // do not create plug view twice because some plugins (e.g., TyrellN6, Podolski) will be crashed.
    if(plug_view_) { return kResultOk; }

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
    // never called if initialization failed.
    assert(component_);
    assert(edit_controller_);
    
	if(status_ == Status::kActivated || status_ == Status::kProcessing) {
		Suspend();
	}
    
    auto cp_comp = queryInterface<Vst::IConnectionPoint>(component_);
    auto cp_edit = queryInterface<Vst::IConnectionPoint>(edit_controller_);
    
    if (cp_comp && cp_edit) {
        cp_comp.right()->disconnect(cp_edit.right().get());
        cp_edit.right()->disconnect(cp_comp.right().get());
    }
    
    edit_controller_->setComponentHandler(nullptr);

	unit_handler_.reset();
	plug_view_.reset();

	if(is_single_component_ == false) {
		edit_controller_->terminate();
	}

	edit_controller2_.reset();

	edit_controller_.reset();
	audio_processor_.reset();

    component_->terminate();
	component_.reset();
}

std::optional<Vst3Plugin::DumpData> Vst3Plugin::Impl::SaveData() const
{
    Vst3Plugin::DumpData ret;
    
    MemoryStream stream;
    auto res = component_->getState(&stream);
    ShowError(res, L"set data to IComponent");
    
    if(res != kResultOk) {
        return std::nullopt;
    }
    
    ret.processor_data_.assign(stream.getData(), stream.getData() + stream.getSize());

    stream = MemoryStream();
    res = edit_controller_->getState(&stream);
    if(res != kResultOk) {
        // do nothing.
    } else {
        ret.edit_controller_data_.assign(stream.getData(), stream.getData() + stream.getSize());
    }
    
    return ret;
}

void Vst3Plugin::Impl::LoadData(DumpData const &dump)
{
    //! Melodyne crashes if a non-owned version of MemoryStream is used.
    MemoryStream stream;
    stream.write((void *)dump.processor_data_.data(), dump.processor_data_.size(), nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    
    if(ShowError(component_->setState(&stream), L"setState") != kResultOk) {
        return;
    }
    
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    ShowError(edit_controller_->setComponentState(&stream), L"setComponentState");
    
    if(dump.edit_controller_data_.empty() == false) {
        stream = MemoryStream();
        stream.write((void *)dump.edit_controller_data_.data(), dump.edit_controller_data_.size(), nullptr);
        stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        
        ShowError(edit_controller_->setState(&stream), L"setState to IEditController");
    }
}

NS_HWM_END
