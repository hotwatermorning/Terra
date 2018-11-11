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

	enum ErrorContext {
		kFactoryError,
		kComponentError,
		kAudioProcessorError,
		kEditControllerError,
		kEditController2Error
	};

	enum class Status {
		kInvalid,
		kCreated,
		kInitialized,
		kSetupDone,
		kActivated,
		kProcessing,
	};

	class Error
    :	public std::runtime_error
	{
    public:
		Error(ErrorContext error_context, tresult error_code)
			:	std::runtime_error("VstPlugin::Error")
			,	error_context_(error_context)
			,	error_code_(error_code)
		{}

		ErrorContext context() { return error_context_; }
		tresult code() { return error_code_; }

	private:
		tresult			error_code_;
		ErrorContext	error_context_;
	};

	class ParameterInfoList
	{
    public:
		typedef Vst::ParameterInfo value_type;
		typedef std::vector<value_type> container;
		typedef container::iterator iterator;
		typedef container::const_iterator const_iterator;
		typedef size_t size_type;

		Vst::ParameterInfo const & GetInfoByID(Vst::ParamID id) const
		{
			return parameters_[IDToIndex(id)];
		}

		Vst::ParameterInfo const & GetInfoByIndex(size_type index) const
		{
			assert(index < parameters_.size());
			return parameters_[index];
		}

		size_type
			IDToIndex(Vst::ParamID id) const
		{
			assert(param_id_to_index_.find(id) != param_id_to_index_.end());
			return param_id_to_index_.find(id)->second;
		}

		void AddInfo(Vst::ParameterInfo const &info)
		{
			parameters_.push_back(info);
			param_id_to_index_[info.id] = parameters_.size()-1;
		}

		size_type size() const { return parameters_.size(); }
		bool empty() const { return parameters_.empty(); }

		const_iterator begin() const { return parameters_.begin(); }
		const_iterator end() const { return parameters_.end(); }

	private:
		std::vector<Vst::ParameterInfo> parameters_;
		std::unordered_map<Vst::ParamID, size_type> param_id_to_index_;
	};

	struct ProgramInfo
	{
		String		        name_;
		Vst::ProgramListID	list_id_;
		Steinberg::int32	index_;
	};
    
    struct BusInfoEx
    {
        Vst::BusInfo bus_info_;
        Vst::SpeakerArrangement speaker_ = Vst::SpeakerArr::kEmpty;
        bool is_active_ = false;
    };
    
    struct AudioBusesInfo
    {
        void Initialize(Impl *owner, Vst::BusDirection dir);
        
        size_t GetNumBuses() const;
        BusInfoEx const & GetBusInfo(size_t bus_index) const;
        
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
        
        Vst::AudioBusBuffers * GetBusBuffers();
        
    private:
        Impl *owner_ = nullptr;
        std::vector<BusInfoEx> bus_infos_;
        Vst::BusDirection dir_;
        
        //! bus_infos_でis_active_がtrueになっているバスの数だけ用意される。
        std::vector<Vst::AudioBusBuffers> bus_buffers_;
        
        void UpdateBusBuffers();
    };

	ParameterInfoList parameters_;
	Steinberg::Vst::ParamID program_change_parameter_;

	std::vector<double> wave_data_;
	int wave_data_index_;

	Status status_;

	Vst::ParameterChanges input_params_;
	Vst::ParameterChanges output_params_;
    Vst::EventList input_events_;
    Vst::EventList output_events_;

public:
	Impl(IPluginFactory *factory,
         ClassInfo const &info,
         FUnknown *host_context);

    ~Impl();

	bool HasEditController	() const;
	bool HasEditController2	() const;

	Vst::IComponent	*		GetComponent		();
	Vst::IAudioProcessor *	GetAudioProcessor	();
	Vst::IEditController *	GetEditController	();
	Vst::IEditController2 *	GetEditController2	();
	Vst::IEditController *	GetEditController	() const;
	Vst::IEditController2 *	GetEditController2	() const;

	String GetEffectName() const;
    
    AudioBusesInfo & GetInputBuses();
    AudioBusesInfo const & GetInputBuses() const;
    AudioBusesInfo & GetOutputBuses();
    AudioBusesInfo const & GetOutputBuses() const;
    
    size_t GetNumParameters() const;

	bool HasEditor() const;

	bool OpenEditor(WindowHandle parent, IPlugFrame *frame);

	void CloseEditor();

	bool IsEditorOpened() const;

	ViewRect GetPreferredRect() const;

	void Resume();

	void Suspend();

	bool IsResumed() const;

	void SetBlockSize(int block_size);

	void SetSamplingRate(int sampling_rate);

	size_t	GetProgramCount() const;

	String GetProgramName(size_t index) const;

	Vst::ParamValue
		NormalizeProgramIndex(size_t index) const;

	size_t DiscretizeProgramIndex(Vst::ParamValue value) const;		

	size_t	GetProgramIndex() const;

	void	SetProgramIndex(size_t index);

	void	RestartComponent(Steinberg::int32 flags);

	void    Process(ProcessInfo pi);

//! Parameter Change
public:
	//! TakeParameterChangesとの呼び出しはスレッドセーフ
	void EnqueueParameterChange(Vst::ParamID id, Vst::ParamValue value);

private:
	//! EnqueueParameterChangeとの呼び出しはスレッドセーフ
	void TakeParameterChanges(Vst::ParameterChanges &dest);

private:
	void LoadPlugin(IPluginFactory *factory, ClassInfo const &info, FUnknown *host_context);

	void LoadInterfaces(IPluginFactory *factory, ClassInfo const &info, FUnknown *host_context);

	void Initialize(vstma_unique_ptr<Vst::IComponentHandler> component_handler);

	tresult CreatePlugView();

	void DeletePlugView();

	void PrepareParameters();

	void PrepareProgramList();

	void UnloadPlugin();

private:
	std::mutex			parameter_queue_mutex_;
	Vst::ParameterChanges	param_changes_queue_;

private:
    std::experimental::optional<ClassInfo> plugin_info_;
	component_ptr_t			component_;
	audio_processor_ptr_t	audio_processor_;
	edit_controller_ptr_t	edit_controller_;
	edit_controller2_ptr_t	edit_controller2_;
	plug_view_ptr_t			plug_view_;
	unit_info_ptr_t			unit_info_;
	program_list_data_ptr_t	program_list_data_;
	Steinberg::int32		current_program_index_;
	std::vector<ProgramInfo> programs_;
	Steinberg::Vst::ParamID	parameter_for_program_; //Presetを表すParameterのID

	Flag					is_processing_started_;
	Flag					edit_controller_is_created_new_;
	Flag					has_editor_;
	Flag					is_editor_opened_;
	Flag					is_resumed_;
	Flag					param_value_changes_was_specified_;

	int	sampling_rate_;
	int block_size_;
    
    void UpdateBusBuffers();
    
    AudioBusesInfo input_buses_info_;
    AudioBusesInfo output_buses_info_;
    
    // Vst3Plugin側にバッファを持たせないで、外側にあるバッファを使い回すほうが、コピーの手間が減っていいが、
    // ちょっと設計がややこしくなるので、いまはここにバッファを持たせるようにしておく。
    Buffer<float> input_buffer_;
    Buffer<float> output_buffer_;
};

NS_HWM_END
