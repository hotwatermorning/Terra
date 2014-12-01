#pragma once

#include <memory>

#include <balor/gui/all.hpp>

#include "Vst3PluginFactory.hpp"

namespace hwm {

//! 複数のコンポーネントを含んでいるモジュールから、コンポーネントを一つ選択するためのダイアログ
class SelectComponentDialog
{
public:
	typedef std::unique_ptr<Steinberg::FUnknown, SelfReleaser> unknown_ptr;
	typedef std::function<void(std::unique_ptr<Vst3PluginFactory> factory, int component_index, unknown_ptr host_context)> on_select_callback;

	SelectComponentDialog();

	void SetCallback(on_select_callback callback);

	bool Create(balor::gui::Frame *owner);

	void RunModal(std::unique_ptr<Vst3PluginFactory> factory, unknown_ptr host_context);

private:
	balor::gui::Frame	frame_;
	balor::gui::ListBox component_list_;
	balor::gui::Button	ok_button_;
	balor::gui::Button	cancel_button_;
	balor::gui::Panel	button_frame_;
	balor::gui::DockLayout	button_layout_;
	balor::gui::DockLayout	layout_;

	std::unique_ptr<Vst3PluginFactory>	factory_;
	unknown_ptr							host_context_;
	on_select_callback callback_;

	void InitializeList();

	void OnOk();

	void OnCancel();
};

} // ::hwm