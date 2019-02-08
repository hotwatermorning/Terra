#pragma once

#include <array>
#include <memory>

#include <functional>

#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstunits.h>

#include "../../processor/ProcessInfo.hpp"
#include "../../misc/ListenerService.hpp"
#include "../../misc/Buffer.hpp"
#include "../../misc/ArrayRef.hpp"
#include "./IdentifiedValueList.hpp"
#include "./Vst3PluginFactory.hpp"

NS_HWM_BEGIN

struct Vst3Note
{
    enum class Type {
        kNoteOn,
        kNoteOff
    };
    
    Vst3Note() = default;
    Vst3Note(SampleCount offset, double ppq_pos, int channel, int pitch, int velocity, Type type)
    {
        SetOffset(offset);
        SetPPQPos(ppq_pos);
        SetChannel(channel);
        SetPitch(pitch);
        SetVelocity(velocity);
        SetNoteType(type);
    }
    
    SampleCount GetOffset() const { return offset_; }
    
    void SetOffset(SampleCount offset) {
        assert(offset_ >= 0);
        offset_ = offset;
    }
    
    double GetPPQPos() const { return ppq_pos_; }
    void SetPPQPos(double ppq_pos) { ppq_pos_ = ppq_pos; }
    
    int GetChannel() const { return channel_; }
    void SetChannel(int channel) {
        assert(0 <= channel && channel <= 15);
        channel_ = channel;
    }
    
    int GetPitch() const { return pitch_; }
    void SetPitch(int pitch) {
        assert(0 <= pitch && pitch <= 127);
        pitch_ = pitch;
    }
    
    int GetVelocity() const { return velocity_; }
    void SetVelocity(int velocity) {
        assert(0 <= velocity && velocity <= 127);
        velocity_ = velocity;
    }
    
    Type GetNoteType() const { return type_; }
    void SetNoteType(Type type) { type_ = type; }
    
    bool IsNoteOn() const { return GetNoteType() == Type::kNoteOn; }
    bool IsNoteOff() const { return GetNoteType() == Type::kNoteOff; }

private:
    SampleCount offset_ = 0;
    double ppq_pos_ = 0;
    int channel_ = 0;
    int pitch_ = 0;
    int velocity_ = 0;
    Type type_ = Type::kNoteOff;
};

//! VST3のプラグインを表すクラス
/*!
	Vst3PluginFactoryから作成可能
	Vst3PluginFactoryによってロードされたモジュール（.vst3ファイル）がアンロードされてしまうため、
	作成したVst3Pluginが破棄されるまで、Vst3PluginFactoryを破棄してはならない。
*/
class Vst3Plugin
{
public:
	class Impl;
    class HostContext;
    
    using ParamID = Steinberg::Vst::ParamID;
    using ParamValue = Steinberg::Vst::ParamValue;
    using ProgramListID = Steinberg::Vst::ProgramListID;
    using UnitID = Steinberg::Vst::UnitID;
    using SpeakerArrangement = Steinberg::Vst::SpeakerArrangement;
    using MediaTypes = Steinberg::Vst::MediaTypes;
    using BusDirections = Steinberg::Vst::BusDirections;
    using BusTypes = Steinberg::Vst::BusTypes;
    
    struct ParameterInfo
    {
        ParamID     id_;                ///< unique identifier of this parameter (named tag too)
        String      title_;        ///< parameter title (e.g. "Volume")
        String      short_title_;    ///< parameter shortTitle (e.g. "Vol")
        String      units_;        ///< parameter unit (e.g. "dB")
        Int32       step_count_;        ///< number of discrete steps (0: continuous, 1: toggle, discrete value otherwise
        ///< (corresponding to max - min, for example: 127 for a min = 0 and a max = 127) - see \ref vst3parameterIntro)
        ParamValue  default_normalized_value_;    ///< default normalized value [0,1] (in case of discrete value: defaultNormalizedValue = defDiscreteValue / stepCount)
        UnitID      unit_id_;            ///< id of unit this parameter belongs to (see \ref vst3UnitsIntro)
        
        bool can_automate_ = false;
        bool is_readonly_ = false;
        bool is_wrap_aound_ = false;
        bool is_list_ = false;
        bool is_program_change_ = false;
        bool is_bypass_ = false;
    };
    
    struct ProgramInfo
    {
        String name_;
        String plugin_name_;
        String plugin_category_;
        String instrument_;
        String style_;
        String character_;
        String state_type_;
        String file_path_string_type_;
        String file_name_;
    };
    
    struct ProgramList
    {
        //! the name of this list
        String name_;
        ProgramListID id_ = Steinberg::Vst::kNoProgramListId;
        std::vector<ProgramInfo> programs_;
    };
    
    struct UnitInfo
    {
        UnitID id_ = Steinberg::Vst::kRootUnitId;
        UnitID parent_id_ = Steinberg::Vst::kNoParentUnitId;
        String name_;
        ProgramList program_list_;
        Steinberg::Vst::ParamID program_change_param_ = Steinberg::Vst::kNoParamId;
    };
    
    using UnitInfoList = IdentifiedValueList<UnitInfo>;
    
    //! @note the value of channel_count_, speaker_, and is_active_ may be changed according to the bus state.
    struct BusInfo
    {
        //!< Media type - has to be a value of @ref Steinberg::Vst::MediaTypes
        Steinberg::Vst::MediaType media_type_;
        
        //!< input or output @ref Steinberg::Vst::BusDirections
        Steinberg::Vst::BusDirection direction_;
        
        //! number of channels
        //! For a bus of type MediaTypes::kEvent the channelCount corresponds
        //! to the number of supported MIDI channels by this bus
        Int32 channel_count_;
        
        //! name of the bus
        String name_;
        
        //! main or aux - has to be a value of @ref Steinerg::Vst::BusTypes
        Steinberg::Vst::BusType bus_type_;
        
        bool is_default_active_ = false;
        
        //! speaker arrangement of the bus.
        //! unused for event buses.
        SpeakerArrangement speaker_ = Steinberg::Vst::SpeakerArr::kEmpty;
        
        //! activation status
        bool is_active_ = false;
    };

public:
	Vst3Plugin(std::unique_ptr<Impl> pimpl,
               std::unique_ptr<HostContext> host_context,
               std::function<void(Vst3Plugin const *p)> on_destruction);
    
	virtual ~Vst3Plugin();
    
    FactoryInfo const & GetFactoryInfo() const;
    ClassInfo const & GetComponentInfo() const;

	String GetEffectName() const;
	size_t	GetNumInputs() const;
    size_t  GetNumOutputs() const;
    
    UInt32  GetNumParams() const;
    ParameterInfo const & GetParameterInfoByIndex(UInt32 index) const;
    ParameterInfo const & GetParameterInfoByID(ParamID id) const;
    
    UInt32  GetNumUnitInfo() const;
    UnitInfo const & GetUnitInfoByIndex(UInt32 index) const;
    UnitInfo const & GetUnitInfoByID(UnitID id) const;
    
    UInt32  GetNumBuses(MediaTypes media, BusDirections dir) const;
    BusInfo const & GetBusInfoByIndex(MediaTypes media, BusDirections dir, UInt32 index) const;
    
    ParamValue GetParameterValueByIndex(UInt32 index) const;
    ParamValue GetParameterValueByID(ParamID id) const;
    
    String ValueToStringByIndex(UInt32 index, ParamValue value);
    ParamValue StringToValueTByIndex(UInt32 index, String string);
    
    String ValueToStringByID(ParamID id, ParamValue value);
    ParamValue StringToValueByID(ParamID id, String string);
    
    bool IsBusActive(MediaTypes media, BusDirections dir, UInt32 index) const;
    void SetBusActive(MediaTypes media, BusDirections dir, UInt32 index, bool state = true);
    UInt32 GetNumActiveBuses(MediaTypes media, BusDirections dir) const;
    SpeakerArrangement GetSpeakerArrangementForBus(BusDirections dir, UInt32 index) const;
    bool SetSpeakerArrangement(BusDirections dir, UInt32 index, SpeakerArrangement arr);
    
	void	Resume();
	void	Suspend();
	bool	IsResumed() const;
	void	SetBlockSize(int block_size);
	void	SetSamplingRate(int sampling_rate);
    
	bool	HasEditor		() const;

#if defined(_MSC_VER)
    using WindowHandle = HWND;
#else
    using WindowHandle = NSView *;
#endif
    
    class PlugFrameListener
    {
    protected:
        PlugFrameListener() {}
    public:
        virtual ~PlugFrameListener() {}
        virtual void OnResizePlugView(Steinberg::ViewRect const &newSize) = 0;
    };
    
    //! listener is not owned in Vst3Plugin. so caller has responsible to delete this object.
    //! listener should never be deleted before CloseEditor() is called.
    bool	OpenEditor		(WindowHandle wnd, PlugFrameListener *listener);
    
	void	CloseEditor		();
	bool	IsEditorOpened	() const;
	Steinberg::ViewRect
			GetPreferredRect() const;

    UInt32  GetProgramIndex(UnitID unit_id = 0) const;
    void    SetProgramIndex(UInt32 index, UnitID unit_id = 0);

	//! パラメータの変更を次回の再生フレームでAudioProcessorに送信して適用するために、
	//! 変更する情報をキューに貯める
	void	EnqueueParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

	void	RestartComponent(Steinberg::int32 flag);

	void Process(ProcessInfo &pi);
    
private:
	std::unique_ptr<Impl> pimpl_;
    std::unique_ptr<HostContext> host_context_;
    std::function<void(Vst3Plugin const *p)> on_destruction_;
};

NS_HWM_END
