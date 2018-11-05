#include "../Vst3Plugin.hpp"

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <atomic>
#include <vector>

#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vstpresetkeys.h"

#include "../Vst3Utils.hpp"
#include "../Vst3Plugin.hpp"
#include "../Vst3PluginFactory.hpp"

#include "../Flag.hpp"
#include "../Buffer.hpp"
#include <experimental/optional>

NS_HWM_BEGIN

using namespace Steinberg;

struct Vst3Plugin::Impl
{
	typedef Impl this_type;

	typedef std::unique_ptr<Vst::IComponent, SelfReleaser>			component_ptr_t;
	typedef std::unique_ptr<Vst::IAudioProcessor, SelfReleaser>		audio_processor_ptr_t;
	typedef std::unique_ptr<Vst::IEditController, SelfReleaser>		edit_controller_ptr_t;
	typedef std::unique_ptr<Vst::IEditController2, SelfReleaser>	edit_controller2_ptr_t;
	typedef std::unique_ptr<Vst::IParameterChanges, SelfReleaser>	parameter_changes_ptr_t;
	typedef std::unique_ptr<IPlugView, SelfReleaser>				plug_view_ptr_t;
	typedef std::unique_ptr<Vst::IUnitInfo, SelfReleaser>			unit_info_ptr_t;
	typedef std::unique_ptr<Vst::IProgramListData, SelfReleaser>	program_list_data_ptr_t;

	enum ErrorContext {
		kFactoryError,
		kComponentError,
		kAudioProcessorError,
		kEditControlError,
		kEditControl2Error
	};

	enum class Status {
		kInvalid,
		kCreated,
		kInitialized,
		kSetupDone,
		kActivated,
		kProcessing,
	};

	struct Error
		:	std::runtime_error
	{
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

	typedef Vst3PluginFactory::host_context_type host_context_type;

	struct ParameterInfoList
	{
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

	ParameterInfoList parameters_;
	Steinberg::Vst::ParamID program_change_parameter_;

	std::vector<double> wave_data_;
	int wave_data_index_;

	Status status_;

	Vst::ParameterChanges input_changes_;
	Vst::ParameterChanges output_changes_;

public:
	Impl(IPluginFactory *factory,
         ClassInfo const &info,
         host_context_type host_context);

    ~Impl();

	Impl(Impl &&) = delete;
	Impl &operator=(Impl &&) = delete;

	bool HasEditController	() const;
	bool HasEditController2	() const;

	Vst::IComponent	*		GetComponent		();
	Vst::IAudioProcessor *	GetAudioProcessor	();
	Vst::IEditController *	GetEditController	();
	Vst::IEditController2 *	GetEditController2	();
	Vst::IEditController *	GetEditController	() const;
	Vst::IEditController2 *	GetEditController2	() const;

	String GetEffectName() const;

	size_t GetNumOutputs() const;

	bool HasEditor() const;

	//bool OpenEditor(HWND parent, IPlugFrame *frame);

	void CloseEditor();

	bool IsEditorOpened() const;

	ViewRect GetPreferredRect() const;

	void Resume();

	void Suspend();

	bool IsResumed() const;

	void SetBlockSize(int block_size);

	void SetSamplingRate(int sampling_rate);

	void	AddNoteOn(int note_number);

	void	AddNoteOff(int note_number);

	size_t	GetProgramCount() const;

	String GetProgramName(size_t index) const;

	Vst::ParamValue
		NormalizeProgramIndex(size_t index) const;

	size_t DiscretizeProgramIndex(Vst::ParamValue value) const;		

	size_t	GetProgramIndex() const;

	void	SetProgramIndex(size_t index);

	void	RestartComponent(Steinberg::int32 flags);

	float ** ProcessAudio(size_t frame_pos, size_t duration);

//! Parameter Change
public:
	//! TakeParameterChangesとの呼び出しはスレッドセーフ
	void EnqueueParameterChange(Vst::ParamID id, Vst::ParamValue value);

private:
	//! EnqueueParameterChangeとの呼び出しはスレッドセーフ
	void TakeParameterChanges(Vst::ParameterChanges &dest);

private:
	void LoadPlugin(IPluginFactory *factory, ClassInfo const &info, host_context_type host_context);

	void LoadInterfaces(IPluginFactory *factory, ClassInfo const &info, Steinberg::FUnknown *host_context);

	void Initialize(std::unique_ptr<Vst::IComponentHandler, SelfReleaser> component_handler);

	tresult CreatePlugView();

	void DeletePlugView();

	void PrepareParameters();

	void PrepareProgramList();

	void UnloadPlugin();

//! デバッグ用関数
private:
	std::wstring
			UnitInfoToString(Steinberg::Vst::UnitInfo const &info);

	std::wstring
			ProgramListInfoToString(Steinberg::Vst::ProgramListInfo const &info);

	std::wstring BusInfoToString(Vst::BusInfo &bus);
	
	std::wstring BusUnitInfoToString(int bus_index, Vst::BusInfo &bus, Vst::IUnitInfo &unit);

	void OutputUnitInfo(Steinberg::Vst::IUnitInfo &info_interface);

	void OutputBusInfoImpl(Vst::IComponent *component, Vst::IUnitInfo *unit, Vst::MediaType media_type, Vst::BusDirection bus_direction);

	void OutputBusInfo(Vst::IComponent *component, Vst::IEditController *edit_controller);

	std::mutex			parameter_queue_mutex_;
	Vst::ParameterChanges	param_changes_queue_;

private:
    std::experimental::optional<ClassInfo> plugin_info_;
	component_ptr_t			component_;
	audio_processor_ptr_t	audio_processor_;
	edit_controller_ptr_t	edit_controller_;
	edit_controller2_ptr_t	edit_controller2_;
	parameter_changes_ptr_t input_parameter_changes_;
	parameter_changes_ptr_t output_parameter_changes_;
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

	struct Note
	{
		enum State {
			kNoteOn,
			kNoteOff
		};

		Note()
			: note_number_(-1)
			, note_state_(State::kNoteOff)
		{}

		int note_number_;
		State note_state_;
	};

	std::mutex note_mutex_;
	std::vector<Note> notes_;

	struct AudioBus
	{
		typedef Buffer<float> buffer_type;

		AudioBus()
			:	speaker_arrangement_(Steinberg::Vst::SpeakerArr::kEmpty)
		{}

		void SetBlockSize(size_t num_samples)
		{
			buffer_.resize_samples(num_samples);
		}

		void SetChannels(size_t num_channels, Steinberg::Vst::SpeakerArrangement speaker_arrangement)
		{
			buffer_.resize_channels(num_channels);
			speaker_arrangement_ = speaker_arrangement;
		}

		size_t channels() const
		{
			return buffer_.channels();
		}

		float **data()
		{
			return buffer_.data();
		}

		float const * const * data() const 
		{
			return buffer_.data();
		}

		Steinberg::Vst::SpeakerArrangement GetSpeakerArrangement() const
		{
			return speaker_arrangement_;
		}

	private:
		buffer_type buffer_;
		Steinberg::uint64 speaker_arrangement_;
	};

	struct AudioBuses
	{
		AudioBuses() 
			:	block_size_(0)
		{}

		AudioBuses(AudioBuses &&rhs)
			:	buses_(std::move(rhs.buses_))
			,	block_size_(rhs.block_size_)
		{}

		AudioBuses & operator=(AudioBuses &&rhs)
		{
			buses_ = std::move(rhs.buses_);
			block_size_ = rhs.block_size_;

			rhs.block_size_ = 0;
			return *this;
		}

		size_t GetBusCount() const
		{
			return buses_.size();
		}

		void SetBusCount(size_t n)
		{
			buses_.resize(n);
		}

		size_t GetBlockSize() const
		{
			return block_size_;
		}

		void SetBlockSize(size_t num_samples)
		{
			for(auto &bus: buses_) {
				bus.SetBlockSize(num_samples);
			}
			block_size_ = num_samples;
		}

		AudioBus & GetBus(size_t index)
		{
			return buses_[index];
		}

		AudioBus const & GetBus(size_t index) const
		{
			return buses_[index];
		}

		void UpdateBufferHeads()
		{
			int n = 0;
			for(auto const &bus: buses_) {
				n += bus.channels();
			}

			std::vector<float *> tmp_heads(n);
			n = 0;
			for(auto &bus: buses_) {
				for(size_t i = 0; i < bus.channels(); ++i) {
					tmp_heads[n] = bus.data()[i];
					++n;
				}
			}

			heads_ = std::move(tmp_heads);
		}

		float ** data()
		{
			return heads_.data();
		}

		float const * const * data() const
		{
			return heads_.data();
		}

		size_t GetTotalChannels() const
		{
			return heads_.size();
		}

	private:
		size_t block_size_;
		std::vector<AudioBus> buses_;
		std::vector<float *> heads_;
	};

	AudioBuses output_buses_;
	AudioBuses input_buses_;
};

NS_HWM_END
