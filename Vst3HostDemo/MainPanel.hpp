#pragma once

#include <boost/function.hpp>
#include <balor/gui/all.hpp>

#include "./Keyboard.hpp"
#include "./ControlPanel.hpp"
#include "EditorFrame.hpp"

namespace hwm {

class MainPanel
{
	typedef boost::function<void(balor::String const &module_file_path)> drop_handler_t;
	typedef boost::function<void(size_t note_number, bool note_on)> note_handler_t;

	typedef ControlPanel::program_select_handler_t		program_select_handler_t;
	typedef ControlPanel::plugin_select_handler_t		plugin_select_handler_t;
	typedef ControlPanel::volume_change_handler_t		volume_change_handler_t;
	typedef ControlPanel::peak_level_query_handler_t	peak_level_query_handler_t;
	typedef ControlPanel::IPluginInfo					IPluginInfo;

private:
	gui::Frame					frame_;
	gui::Panel					panel_;
	Keyboard					keyboard_;
	ControlPanel				control_panel_;
	EditorFrame					editor_;
	gui::DragDrop::Target		dnd_target_;

	drop_handler_t drop_handler_;

public:
	MainPanel(balor::String title, int width, int height);
	~MainPanel();

	//! プラグインをドラッグ&ドロップした時に呼ばれるハンドラを設定
	void SetDropHandler(drop_handler_t handler);

	//! プログラム（パラメータのプリセット）が選択された時に呼ばれるハンドラを設定
	void SetProgramSelectHandler(program_select_handler_t handler);

	//! ファイルダイアログからプラグインが選択された時に呼ばれるハンドラを設定
	void SetPluginSelectHandler(plugin_select_handler_t handler);

	//! ボリュームフェーダーが操作された時に呼ばれるハンドラを設定
	void SetVolumeChangeHandler(volume_change_handler_t handler);

	//! 音量の問い合わせ要求が来た時に呼ばれるハンドラを設定
	void SetQueryLevelHandler(peak_level_query_handler_t handler);

	//! 鍵盤が押されてノート情報が送信された時に呼ばれるハンドラを設定
	void SetNoteHandler(note_handler_t handler);

	void SetNumChannels(size_t channels);

	void AssignPluginToEditorFrame(
			balor::String title,
			boost::function<Steinberg::ViewRect(HWND wnd, IPlugFrame *frame)> assignment_function );

	void DeassignPluginFromEditorFrame(boost::function<void()> deassignment_function);

	void SetPluginInfo(IPluginInfo const &info);
	void ClearPluginInfo();

	//! メッセージループを開始
	void Run();

	gui::Frame * GetMainFrame();
};

}