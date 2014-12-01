#include "MainPanel.hpp"
#include "balor/io/all.hpp"

namespace hwm {

namespace {
	static size_t const CONTROL_PANEL_HEIGHT = 150;
	static size_t const CLIENT_HEIGHT		= 200;
	static size_t const KEYBOARD_HEIGHT		= 50;
}

MainPanel::MainPanel(balor::String title, int width, int height)
	:	frame_(title, width, height, gui::Frame::Style::singleLine)
	,	panel_(frame_, 0, 0, width, height)
	,	control_panel_(panel_, 0, 0, width, CONTROL_PANEL_HEIGHT)
	,	keyboard_(panel_, 0, height - KEYBOARD_HEIGHT, width, KEYBOARD_HEIGHT)
	,	editor_()
{
	editor_.owner(&frame_);

	//! Drag & Dropの設定
	//! .vst3ファイルだけ受け付ける。
	dnd_target_ = gui::DragDrop::Target(panel_);
	dnd_target_.onDrop() = [this] (gui::DragDrop::Drop &e) {
		if(e.data().containsFileDropList()) {
			auto file_list = e.data().getFileDropList();
			if(file_list.empty()) { return; }

			if(io::File(file_list[0]).extension() == L".vst3") {
				balor::String module_file_path = file_list[0];
				drop_handler_(module_file_path);
			}
		}
	};
	dnd_target_.onMove() = [this] (gui::DragDrop::Move &e) {
		if(!e.data().containsFileDropList()) {
			e.effect(gui::DragDrop::Effect::none);
		}
	};
}

MainPanel::~MainPanel() {}

void MainPanel::SetDropHandler(drop_handler_t handler)
{
	drop_handler_ = handler;
}

void MainPanel::SetProgramSelectHandler(program_select_handler_t handler)
{
	control_panel_.SetProgramSelectHandler(handler);
}

void MainPanel::SetPluginSelectHandler(plugin_select_handler_t handler)
{
	control_panel_.SetPluginSelectHandler(handler);
}

void MainPanel::SetVolumeChangeHandler(volume_change_handler_t handler)
{
	control_panel_.SetVolumeChangeHandler(handler);
}

void MainPanel::SetQueryLevelHandler(peak_level_query_handler_t handler)
{
	control_panel_.SetQueryLevelHandler(handler);
}

void MainPanel::SetNoteHandler(note_handler_t handler)
{
	keyboard_.SetNoteHandler(handler);
}

void MainPanel::SetNumChannels(size_t channels)
{
	control_panel_.SetNumChannels(channels);
}

void MainPanel::AssignPluginToEditorFrame(
		balor::String title,
		boost::function<Steinberg::ViewRect(HWND wnd, IPlugFrame *frame)> assignment_function)
{
	auto pos = frame_.position();
	pos += balor::Size(0, frame_.size().height);
	Steinberg::ViewRect rc = assignment_function(editor_.GetHandle(), &editor_);
	editor_.resizeView(rc);
	editor_.visible(true);
}

void MainPanel::DeassignPluginFromEditorFrame(boost::function<void()> deassignment_function)
{
	deassignment_function();
	editor_.visible(false);
}

void MainPanel::SetPluginInfo(IPluginInfo const &info)
{
	control_panel_.SetPluginInfo(info);
}

void MainPanel::ClearPluginInfo()
{
	control_panel_.ClearPluginInfo();
}

void MainPanel::Run()
{
	frame_.runMessageLoop();
}

gui::Frame * MainPanel::GetMainFrame()
{
	return &frame_;
}

}