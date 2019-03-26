#include "Vst3Plugin.hpp"

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <atomic>
#include <vector>

#include <pluginterfaces/base/ftypes.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <pluginterfaces/vst/ivstunits.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/base/ustring.h>
#include <pluginterfaces/vst/vstpresetkeys.h>

#include "Vst3Utils.hpp"
#include "Vst3Plugin.hpp"
#include "Vst3PluginFactory.hpp"

#include "../../misc/Flag.hpp"
#include "../../misc/Buffer.hpp"
#include "../../misc/LockFactory.hpp"

NS_HWM_BEGIN

using namespace Steinberg;

class Vst3Plugin::Impl
{
public:
	typedef Impl this_type;

	using component_ptr_t           = vstma_unique_ptr<Vst::IComponent>;
	using audio_processor_ptr_t     = vstma_unique_ptr<Vst::IAudioProcessor>;
	using edit_controller_ptr_t     = vstma_unique_ptr<Vst::IEditController>;
	using edit_controller2_ptr_t    = vstma_unique_ptr<Vst::IEditController2>;
	using parameter_changes_ptr_t   = vstma_unique_ptr<Vst::IParameterChanges>;
	using plug_view_ptr_t           = vstma_unique_ptr<IPlugView>;
	using unit_info_ptr_t           = vstma_unique_ptr<Vst::IUnitInfo>;
	using program_list_data_ptr_t   = vstma_unique_ptr<Vst::IProgramListData>;

	enum class Status {
		kInvalid,
		kCreated,
		kInitialized,
		kSetupDone,
		kActivated,
		kProcessing,
	};
    
    using ParameterInfoList = IdentifiedValueList<ParameterInfo>;
    using UnitInfoList = IdentifiedValueList<UnitInfo>;
    
    struct MidiBusesInfo
    {
        void Initialize(Impl *owner, Vst::BusDirections dir);
        
        size_t GetNumBuses() const;
        
        BusInfo const & GetBusInfo(UInt32 bus_index) const;
        
        bool IsActive(size_t bus_index) const;
        void SetActive(size_t bus_index, bool state = true);
        
        UInt32 GetNumActiveBuses() const;
        UInt32 GetBusIndexFromActiveBusIndex(UInt32 active_bus_index) const;
        UInt32 GetActiveBusIndexFromBusIndex(UInt32 bus_index) const;
        
        Vst::AudioBusBuffers * GetBusBuffers();
        
    private:
        Impl *owner_ = nullptr;
        std::vector<BusInfo> bus_infos_;
        Vst::BusDirections dir_;
        std::unordered_map<UInt32, UInt32> bus_index_to_active_bus_index_;
        std::unordered_map<UInt32, UInt32> active_bus_index_to_bus_index_;
        
        void SetupActiveBusTable();
    };
    
    struct AudioBusesInfo
    {
        void Initialize(Impl *owner, Vst::BusDirections dir);
        
        size_t GetNumBuses() const;
        
        BusInfo const & GetBusInfo(UInt32 bus_index) const;
        
        //! すべてのバスのチャンネル数の総計
        //! これは、各バスのSpeakerArrangement状態によって変化する。
        //! バスのアクティブ状態には影響を受けない。
        //!  (つまり、各バスがアクティブかそうでないかに関わらず、すべてのバスのチャンネルが足し合わされる。)
        size_t GetNumChannels() const;

        //! すべてのアクティブなバスのチャンネル数の総計
        //! これは、各バスのアクティブ状態やSpeakerArrangement状態によって変化する。
        size_t GetNumActiveChannels() const;
        
        bool IsActive(size_t bus_index) const;
        void SetActive(size_t bus_index, bool state = true);
        
        //! @return true if this speaker arrangement is accepted to the plugin successfully,
        //! false otherwise.
        bool SetSpeakerArrangement(size_t bus_index, Vst::SpeakerArrangement arr);
        
        std::vector<Vst::SpeakerArrangement> GetSpeakers() const;
        Vst::AudioBusBuffers * GetBusBuffers();
        
    private:
        Impl *owner_ = nullptr;
        std::vector<BusInfo> bus_infos_;
        Vst::BusDirections dir_; // determine direction even if the bus_infos_ is empty.
        
        //! bus_infos_のis_active_状態によらず、定義されているすべてのバスと同じ数だけ用意される。
        std::vector<Vst::AudioBusBuffers> bus_buffers_;
        
        void UpdateBusBuffers();
    };

public:
	Impl(IPluginFactory *factory,
         FactoryInfo const &factory_info,
         ClassInfo const &class_info,
         FUnknown *host_context);

    ~Impl();
    
    FactoryInfo const & GetFactoryInfo() const;
    ClassInfo const & GetComponentInfo() const;

	bool HasEditController	() const;
	bool HasEditController2	() const;

	Vst::IComponent	*		GetComponent		();
	Vst::IAudioProcessor *	GetAudioProcessor	();
	Vst::IEditController *	GetEditController	();
	Vst::IEditController2 *	GetEditController2	();
	Vst::IEditController *	GetEditController	() const;
	Vst::IEditController2 *	GetEditController2	() const;

	String GetEffectName() const;
    
    ParameterInfoList & GetParameterInfoList();
    ParameterInfoList const & GetParameterInfoList() const;
    
    UnitInfoList & GetUnitInfoList();
    UnitInfoList const & GetUnitInfoList() const;
    
    AudioBusesInfo & GetAudioBusesInfo(BusDirections dir);
    AudioBusesInfo const & GetAudioBusesInfo(BusDirections dir) const;
    MidiBusesInfo & GetMidiBusesInfo(BusDirections dir);
    MidiBusesInfo const & GetMidiBusesInfo(BusDirections dir) const;
    
    UInt32 GetNumParameters() const;
    Vst::ParamValue GetParameterValueByIndex(UInt32 index) const;
    Vst::ParamValue GetParameterValueByID(Vst::ParamID id) const;
    void SetParameterValueByIndex(UInt32 index, Vst::ParamValue value);
    void SetParameterValueByID(Vst::ParamID id, Vst::ParamValue value);
    
    String ValueToStringByIndex(UInt32 index, ParamValue value);
    ParamValue StringToValueTByIndex(UInt32 index, String string);
    
    String ValueToStringByID(ParamID id, ParamValue value);
    ParamValue StringToValueByID(ParamID id, String string);
    
    UInt32  GetProgramIndex(Vst::UnitID unit_id = 0) const;
    void    SetProgramIndex(UInt32 index, Vst::UnitID unit_id = 0);

	bool HasEditor() const;
    
    // Call this function once before to call HasEditor().
    void CheckHavingEditor();

	bool OpenEditor(WindowHandle parent, IPlugFrame *plug_frame);

	void CloseEditor();

	bool IsEditorOpened() const;

	ViewRect GetPreferredRect() const;

	void Resume();

	void Suspend();

	bool IsResumed() const;

	void SetBlockSize(int block_size);

	void SetSamplingRate(int sampling_rate);

	void	RestartComponent(Steinberg::int32 flags);

	void    Process(ProcessInfo pi);
    
    std::optional<DumpData> SaveData() const;
    void LoadData(DumpData const &dump);

//! Parameter Change
public:
	//! PopFrontParameterChangesとの呼び出しはスレッドセーフ
	void PushBackParameterChange(Vst::ParamID id, Vst::ParamValue value, SampleCount offset = 0);
    
private:
    //! PushBackParameterChangeとの呼び出しはスレッドセーフ
    void PopFrontParameterChanges(Vst::ParameterChanges &dest);
    
    void InputEvents(ProcessInfo::IEventBufferList const *buffers,
                     Vst::ProcessContext const &process_context);
    
    void OutputEvents(ProcessInfo::IEventBufferList *buffers,
                      Vst::ProcessContext const &process_context);

private:
    //! create and initialize components, pass the host_context to the components, obtain interfaces.
	void LoadInterfaces(IPluginFactory *factory, ClassInfo const &info, FUnknown *host_context);
    //! initialize this instance with loaded interfaces.
	void Initialize();

	tresult CreatePlugView();
	void DeletePlugView();

	void PrepareParameters();
	void PrepareUnitInfo();

	void UnloadPlugin();

private:
    ClassInfo               class_info_;
    FactoryInfo             factory_info_;
	component_ptr_t			component_;
	audio_processor_ptr_t	audio_processor_;
	edit_controller_ptr_t	edit_controller_;
	edit_controller2_ptr_t	edit_controller2_;
	plug_view_ptr_t			plug_view_;
	unit_info_ptr_t			unit_handler_;
    UnitInfoList            unit_info_list_;
    ParameterInfoList       parameter_info_list_;
    vstma_unique_ptr<Vst::IMidiMapping> midi_mapping_;

    Vst::ProcessSetup       applied_process_setup_ = {};
    
    //! represents that this plugin do not split components.
	Flag					is_single_component_;
	Flag					has_editor_;
	Flag					is_editor_opened_;
	Flag					param_value_changes_was_specified_;

	int	sampling_rate_;
	int block_size_;
    
    void UpdateBusBuffers();
    
    AudioBusesInfo input_audio_buses_info_;
    AudioBusesInfo output_audio_buses_info_;
    MidiBusesInfo input_midi_buses_info_;
    MidiBusesInfo output_midi_buses_info_;
    
    // Vst3Plugin側にバッファを持たせないで、外側にあるバッファを使い回すほうが、コピーの手間が減っていいが、
    // ちょっと設計がややこしくなるので、いまはここにバッファを持たせるようにしておく。
    Buffer<float> input_buffer_;
    Buffer<float> output_buffer_;
    
    std::atomic<Status> status_;
    
private:
    LockFactory lf_processing_;
    LockFactory lf_parameter_queue_;
    Vst::ParameterChanges   param_changes_queue_;
    
    Vst::ParameterChanges input_params_;
    Vst::ParameterChanges output_params_;
    Vst::EventList input_events_;
    Vst::EventList output_events_;
};

NS_HWM_END
