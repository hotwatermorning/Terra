#pragma once

#include <memory>

#include <boost/thread/mutex.hpp>

#include <balor/gui/all.hpp>

#include "Vst3PluginFactory.hpp"
#include "Vst3Plugin.hpp"
#include "Vst3HostCallback.hpp"
#include "./WaveOutProcessor.hpp"
#include "./PeakMeter.hpp"
#include "./MainPanel.hpp"
#include "./SelectComponentDialog.hpp"

namespace hwm {

class VstHostDemo
{
private:
	WaveOutProcessor			waveout_;
	size_t						sampling_rate_;
	size_t						block_size_;
	std::unique_ptr<Vst3PluginFactory> vst3_plugin_factory_;			
	std::unique_ptr<Vst3Plugin> vst3_plugin_;
	Vst3HostCallback			vst3_host_;
	double						dB_;
	boost::mutex mutable		process_mutex_;
	std::vector<PeakMeter>		peak_meters_;
	MainPanel					main_panel_;
	SelectComponentDialog		select_component_;

public:
	VstHostDemo();
	~VstHostDemo();

private:
	VstHostDemo(VstHostDemo const &);
	VstHostDemo & operator=(VstHostDemo const &);

public:
	bool OpenDevice(int sampling_rate, int num_channels, size_t block_size);
	void CloseDevice();

	//! 新たにプラグインをロードする。
	//! 以前にロードされていたプラグインはアンロードされる。
	void LoadPlugin(balor::String module_path);
	void UnloadPlugin();

	//! メッセージループを開始する
	void Run();

private:
	void OnNoteOn(size_t note_number, bool note_on);
	void OnSelectProgram(size_t selected_index);
	void OnChangeVolume(double dB);
	void OnQueryPeakLevel(PeakLevel *pl, size_t num_channels) const;

	void OpenEditor();
	void CloseEditor();

	void OnProcess(short *data, size_t num_channels, size_t num_samples);

	void LoadPluginImpl(std::unique_ptr<Vst3PluginFactory> factory, std::unique_ptr<Vst3Plugin> plugin);
};

}	//::hwm