#pragma once

#include <array>
#include <memory>

#include <functional>

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "./Vst3Utils.hpp"

NS_HWM_BEGIN
    
using String = std::wstring;

//! VST3のプラグインを表すクラス
/*!
	Vst3PluginFactoryから作成可能
	Vst3PluginFactoryによってロードされたモジュール（.vst3ファイル）がアンロードされてしまうため、
	作成したVst3Pluginが破棄されるまで、Vst3PluginFactoryを破棄してはならない。
	=> モジュールをshared_ptrで管理してそれぞれのVst3Pluginに持たせてもいいかもしれない
*/
class Vst3Plugin
{
public:
	struct Impl;

	struct ParameterAccessor
	{
		ParameterAccessor(Vst3Plugin *owner);

		typedef Steinberg::Vst::ParamValue value_t;

		size_t	size() const;

		value_t get_by_index(size_t index) const;
		void	set_by_index(size_t index, value_t new_value);
		
		value_t get_by_id	(Steinberg::Vst::ParamID id) const;
		void	set_by_id	(Steinberg::Vst::ParamID id, value_t value);

		Steinberg::Vst::ParameterInfo
				info(size_t index) const;

	private:
		Vst3Plugin *owner_;
	};
	friend ParameterAccessor;

public:
	Vst3Plugin(std::unique_ptr<Impl> pimpl,
               std::function<void(Vst3Plugin const *p)> on_destruction);
	virtual ~Vst3Plugin();

	ParameterAccessor &			GetParams();
	ParameterAccessor const &	GetParams() const;

	String GetEffectName() const;
	size_t	GetNumOutputs() const;
	void	Resume();
	void	Suspend();
	bool	IsResumed() const;
	void	SetBlockSize(int block_size);
	void	SetSamplingRate(int sampling_rate);

	bool	HasEditor		() const;
	//bool	OpenEditor		(HWND wnd, Steinberg::IPlugFrame *frame);
	void	CloseEditor		();
	bool	IsEditorOpened	() const;
	Steinberg::ViewRect
			GetPreferredRect() const;

	void	AddNoteOn(int note_number);
	void	AddNoteOff(int note_number);

	size_t	GetProgramCount() const;
	String  GetProgramName(size_t index) const;

	size_t	GetProgramIndex() const;

	void	SetProgramIndex(size_t index);

	//! パラメータの変更を次回の再生フレームでAudioProcessorに送信して適用するために、
	//! 変更する情報をキューに貯める
	void	EnqueueParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

	void	RestartComponent(Steinberg::int32 flag);

	float ** ProcessAudio(size_t frame_pos, size_t num_samples);

private:

	//! deleted
	Vst3Plugin(Vst3Plugin const &) = delete;
	//! deleted
	Vst3Plugin & operator=(Vst3Plugin const &) = delete;

	std::unique_ptr<ParameterAccessor>	parameters_;
	std::unique_ptr<Impl>		pimpl_;
    std::function<void(Vst3Plugin const *p)> on_destruction_;
};

NS_HWM_END
