#include <cmath>
#include <array>
#include <balor/locale/Charset.hpp>

#include "./VstHostDemo.hpp"
#include <boost/thread/lock_factories.hpp>
#include "debugger_output.hpp"
#include "vst3/pluginterfaces/vst/ivstaudioprocessor.h"

namespace {
	balor::locale::Charset converter = balor::locale::Charset::ascii();

	std::wostream & operator<<(std::wostream &os, balor::String const &str)
	{
		os << str.c_str();
		return os;
	}
}

namespace hwm {

static size_t const CLIENT_WIDTH = 800;
static size_t const PROPERTY_WINDOW_HEIGHT = 150;
static size_t const CLIENT_HEIGHT = 200;
static size_t const KEYBOARD_HEIGHT = 50;

gpx::GdiplusInitializer gdiplus;

double dB_to_linear(double dB)
{
	if(dB <= -96.0) {
		return 0;
	} else {
		return pow(10.0, dB/20.0);
	}
}

double linear_to_dB(double linear)
{
	static double const dB_640 = 0.00000000000000000000000000000001;
	if(linear < dB_640) {
		return -640;
	} else {
		return 20.0 * log10(linear);
	}
}

balor::String GetApplicationName()
{
#if defined(_WIN64)
	return L"VstHostDemo - x64";
#else
	return L"VstHostDemo - x86";
#endif
}

VstHostDemo::VstHostDemo()
:	main_panel_(GetApplicationName(), CLIENT_WIDTH, CLIENT_HEIGHT)
,	dB_(0)
,	select_component_()
{
	main_panel_.SetNoteHandler([this](size_t note_number, bool note_on) {
		OnNoteOn(note_number, note_on);
	});

	main_panel_.SetProgramSelectHandler([this](size_t selected_index) {
		OnSelectProgram(selected_index);
	});

	main_panel_.SetPluginSelectHandler([this](balor::String module_path) {
		LoadPlugin(module_path);
	});

	main_panel_.SetVolumeChangeHandler([this](double dB) {
		OnChangeVolume(dB);
	});

	main_panel_.SetQueryLevelHandler([this](PeakLevel *pl, size_t num_channels) {
		return OnQueryPeakLevel(pl, num_channels);
	});

	main_panel_.SetDropHandler([this] (balor::String module_file_path) {
		LoadPlugin(module_file_path);
	});

	vst3_host_.SetRequestToRestartHandler([this](Steinberg::int32 flags) {
		if(vst3_plugin_) {
			vst3_plugin_->RestartComponent(flags);
		}
	});

	vst3_host_.SetParameterChangeNotificationHandler([this](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
			vst3_plugin_->EnqueueParameterChange(id, value);
		});

	select_component_.SetCallback(
		[this] (std::unique_ptr<Vst3PluginFactory> factory, int component_index, Vst3HostCallback::unknown_ptr host_context) {
			std::unique_ptr<Vst3Plugin> plugin = factory->CreateByIndex(component_index, std::move(host_context));
			LoadPluginImpl(std::move(factory), std::move(plugin));
		});

	select_component_.Create(main_panel_.GetMainFrame());
}

VstHostDemo::~VstHostDemo()
{
	CloseEditor();
	UnloadPlugin();
	CloseDevice();
}

bool VstHostDemo::OpenDevice(int sampling_rate, int num_channels, size_t block_size)
{
	auto lock = boost::make_unique_lock(process_mutex_);

	bool const open_result = waveout_.OpenDevice(
		sampling_rate,
		num_channels,
		block_size,
		3,
		[this](short *data, size_t num_channels, size_t num_samples) {
			OnProcess(data, num_channels, num_samples);
		});

	if(open_result) {
		sampling_rate_ = sampling_rate;
		block_size_ = block_size;
		main_panel_.SetNumChannels(num_channels);
		std::vector<PeakMeter> tmp_meters;
		for(size_t i = 0; i < num_channels; ++i) {
			tmp_meters.emplace_back(sampling_rate);
			tmp_meters.back().SetHoldTime(400);
			tmp_meters.back().SetReleaseSpeed(-196.0);
		}
		peak_meters_.swap(tmp_meters);
	}

	return open_result;
}

void VstHostDemo::CloseDevice()
{
	waveout_.CloseDevice();
}

struct Vst3PluginInfo 
	:	ControlPanel::IPluginInfo
{
	Vst3PluginInfo(Vst3Plugin const *plugin)
		:	plugin_(plugin)
	{}

	balor::String
		GetName() const override
	{
		return plugin_->GetEffectName();
	}

	size_t
		GetProgramCount() const override
	{
		return plugin_->GetProgramCount();
	}

	balor::String
		GetProgramName(size_t n) const override
	{
		return plugin_->GetProgramName(n);
	}

private:
	Vst3Plugin const *plugin_;
};

std::wstring
		ParameterInfoToString(Steinberg::Vst::ParameterInfo const &info)
{
	typedef Steinberg::Vst::ParameterInfo::ParameterFlags Flags;
	std::wstringstream ss;
	ss	<< info.id
		<< L", " << info.shortTitle 
		<< L", " << info.title
		<< L", " << info.units
		<< L", " << info.stepCount
		<< L", " << info.defaultNormalizedValue
		<< L", " << info.unitId
		<< std::boolalpha
		<< L", " << L"Can Automate: " << ((info.flags & Flags::kCanAutomate) != 0)
		<< L", " << L"Is Read Only: " << ((info.flags & Flags::kIsReadOnly) != 0)
		<< L", " << L"Is Wrap Around: " << ((info.flags & Flags::kIsWrapAround) != 0)
		<< L", " << L"Is List: " << ((info.flags & Flags::kIsList) != 0)
		<< L", " << L"Is Program Change: " << ((info.flags & Flags::kIsProgramChange) != 0)
		<< L", " << L"Is Bypass: " << ((info.flags & Flags::kIsBypass) != 0);

	return ss.str();
}

void OutputParameterInfo(Vst3Plugin::ParameterAccessor const &params)
{
	hwm::wdout << "--- Output Parameter Info ---" << std::endl;
	for(int i = 0; i < params.size(); ++i) {
		hwm::wdout
			<< L"[" << i << L"] "
			<< ParameterInfoToString(params.info(i))
			<< L"\n";
	}
}

#define kLVstAudioEffectClass L"Audio Module Class"

int GetAudioModuleComponentCount(Vst3PluginFactory *factory)
{
	int count = 0;
	for(int i = 0; i < factory->GetComponentCount(); ++i) {
		if(factory->GetComponentInfo(i).category() == kLVstAudioEffectClass) {
			++count;
		}
	}
	return count;
}

void VstHostDemo::LoadPlugin(balor::String module_path)
{
	std::unique_ptr<Vst3PluginFactory> new_factory;
	std::unique_ptr<Vst3Plugin> new_plugin;
	try {
		new_factory = std::unique_ptr<Vst3PluginFactory>(new Vst3PluginFactory(module_path));
		int const component_count = GetAudioModuleComponentCount(new_factory.get());

		if(component_count == 0) {
			return;
		} else if(component_count == 1) {
			new_plugin = new_factory->CreateByIndex(0, vst3_host_.GetUnknownPtr());
			LoadPluginImpl(std::move(new_factory), std::move(new_plugin));
		} else {
			select_component_.RunModal(std::move(new_factory), vst3_host_.GetUnknownPtr());			
		}
	} catch(std::exception &e) {
		hwm::dout << "Error in vst3 : " << e.what() << std::endl;
		return;
	}
}

void VstHostDemo::LoadPluginImpl(std::unique_ptr<Vst3PluginFactory> new_factory, std::unique_ptr<Vst3Plugin> new_plugin)
{
	CloseEditor();

	auto lock = boost::make_unique_lock(process_mutex_);

	vst3_plugin_.reset(); // plugin must be destructed before its factory was destructed.
	vst3_plugin_factory_.reset();

	vst3_plugin_factory_ = std::move(new_factory);
	vst3_plugin_ = std::move(new_plugin);

	OutputParameterInfo(vst3_plugin_->GetParams());

	vst3_plugin_->SetBlockSize(block_size_);
	vst3_plugin_->SetSamplingRate(sampling_rate_);
	vst3_plugin_->Resume();

	main_panel_.ClearPluginInfo();
	main_panel_.SetPluginInfo(Vst3PluginInfo(vst3_plugin_.get()));
	
	lock.unlock();

	OpenEditor();

	return;
}

void VstHostDemo::UnloadPlugin()
{
	std::unique_ptr<Vst3PluginFactory> tmp_factory;
	std::unique_ptr<Vst3Plugin> tmp_plugin;

	{
		auto lock = boost::make_unique_lock(process_mutex_);

		if(vst3_plugin_) {

			CloseEditor();

			if(vst3_plugin_->IsResumed()) {
				vst3_plugin_->Suspend();
			}

			boost::swap(tmp_factory, vst3_plugin_factory_);
			boost::swap(tmp_plugin, vst3_plugin_);
		}
	}
}

void VstHostDemo::OpenEditor()
{
	if(!vst3_plugin_->HasEditor()) { return; }

	main_panel_.AssignPluginToEditorFrame(
		vst3_plugin_->GetEffectName(),
		[this] (HWND parent, Steinberg::IPlugFrame *frame) {
			vst3_plugin_->OpenEditor(parent, frame);
			Steinberg::ViewRect rc = vst3_plugin_->GetPreferredRect();
			return rc;
		});
}

void VstHostDemo::CloseEditor()
{
	if(vst3_plugin_ && vst3_plugin_->IsEditorOpened()) {
		main_panel_.DeassignPluginFromEditorFrame([this]() {
			vst3_plugin_->CloseEditor();
		});
	}
}

void VstHostDemo::Run()
{
	main_panel_.Run();
}

void VstHostDemo::OnNoteOn(size_t note_number, bool note_on)
{
	auto lock = boost::make_unique_lock(process_mutex_);

	if(!vst3_plugin_) {
		return;
	}

	if(note_on) {
		vst3_plugin_->AddNoteOn(note_number);
	} else {
		vst3_plugin_->AddNoteOff(note_number);
	}
}

void VstHostDemo::OnSelectProgram(size_t selected_index)
{
	auto lock = boost::make_unique_lock(process_mutex_);

	if(!vst3_plugin_) {
		return;
	}

	hwm::dout 
		<< "current program index : " << vst3_plugin_->GetProgramIndex() << "\n"
		<< "selected program index : " << selected_index << std::endl;

	vst3_plugin_->SetProgramIndex(selected_index);
}

void VstHostDemo::OnChangeVolume(double dB)
{
	auto lock = boost::make_unique_lock(process_mutex_);
	dB_ = dB;
}

void VstHostDemo::OnQueryPeakLevel(PeakLevel *pl, size_t num_channels) const
{
	auto lock = boost::make_unique_lock(process_mutex_);

	BOOST_ASSERT(num_channels == peak_meters_.size());
	for(size_t ch = 0; ch < num_channels; ++ch) {
		pl[ch] = PeakLevel(peak_meters_[ch].GetPeak(), peak_meters_[ch].GetLevel());
	}
}

void VstHostDemo::OnProcess(short *data, size_t num_channels, size_t num_samples)
{
	auto lock = boost::make_unique_lock(process_mutex_);

	if(!vst3_plugin_) {
		lock.unlock();
		std::fill(data, data + num_channels * num_samples, 0);
		return;
	}

	static size_t pos; 

	//! VstPluginに追加したノートイベントを
	//! 再生用データとして実際のプラグイン内部に渡す
	//plugin_->ProcessEvents();
				
	//! sample分の時間のオーディオデータ合成
	size_t const num_plugin_channels = vst3_plugin_->GetNumOutputs();
	size_t const num_device_channels = num_channels;

	float **synthesized = vst3_plugin_->ProcessAudio(pos, num_samples);
	pos += num_samples;

	//! 合成したデータをオーディオデバイスのチャンネル数以内のデータ領域に書き出し。
	//! デバイスのサンプルタイプを16bit整数で開いているので、
	//! VST側の-1.0 .. 1.0のオーディオデータを-32768 .. 32767に変換している。
	//! また、VST側で合成したデータはチャンネルごとに列が分かれているので、
	//! Waveformオーディオデバイスに流す前にインターリーブする。

	double const gain = dB_to_linear(dB_);

	for(size_t ch = 0; ch < num_device_channels; ++ch) {
		if(ch >= num_plugin_channels) {
			for(size_t smp = 0; smp < num_samples; ++smp) {
				data[smp * num_device_channels + ch] = 0;
			}

			peak_meters_[ch].Consume(num_samples);

		} else {
			for(size_t smp = 0; smp < num_samples; ++smp) {
				double const sample = synthesized[ch][smp] * gain * 32768.0;
				data[smp * num_device_channels + ch] =
					static_cast<short>(
						std::max<double>(-32768.0, std::min<double>(sample, 32767.0))
						);
			}

			peak_meters_[ch].SetSamples(
				synthesized[ch], synthesized[ch] + num_samples,
				[gain](double x) { return linear_to_dB(x * gain); } );
		}
	}
}

}