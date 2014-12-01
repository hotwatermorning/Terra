#include "SelectComponentDialog.hpp"

namespace hwm {

SelectComponentDialog::SelectComponentDialog()
{}

void SelectComponentDialog::SetCallback(on_select_callback callback)
{
	callback_ = callback;
}

bool SelectComponentDialog::Create(balor::gui::Frame *owner)
{
	frame_ = balor::gui::Frame(L"Select Component", 400, 600);
	frame_.owner(owner);
	button_frame_ = balor::gui::Panel(frame_, 0, 0, 0, 60);

	component_list_ = balor::gui::ListBox(frame_, 0, 0, 0, 0);

	ok_button_ = balor::gui::Button(
		button_frame_, 0, 0, 200, 0, L"OK",
		[this] (balor::gui::Button::Event &) { OnOk(); }
	);

	cancel_button_ = balor::gui::Button(
		button_frame_, 0, 0, 200, 0, L"Cancel",
		[this] (balor::gui::Button::Event &) { OnCancel(); }
	);

	layout_ = balor::gui::DockLayout(frame_);
	button_layout_ = balor::gui::DockLayout(button_frame_);

	layout_.setStyle(button_frame_, balor::gui::DockLayout::Style::bottom);
	layout_.setStyle(component_list_, balor::gui::DockLayout::Style::fill);
	button_layout_.setStyle(ok_button_, balor::gui::DockLayout::Style::left);
	button_layout_.setStyle(cancel_button_, balor::gui::DockLayout::Style::left);

	frame_.onResize() = [&](balor::gui::Frame::Resize &event) {
		layout_.perform();
		button_layout_.perform();
	};

	layout_.setMinimumSize(component_list_, balor::Size(100, 400));
	frame_.onResizing() = [&] (balor::gui::Frame::Resizing& e) {
		e.minTrackSize(layout_.preferredSize());
	};
	return true;
}

void SelectComponentDialog::RunModal(std::unique_ptr<Vst3PluginFactory> factory, unknown_ptr host_context)
{
	factory_ = std::move(factory);
	host_context_ = std::move(host_context);

	InitializeList();
	frame_.runModalMessageLoop();
}

//! コンポーネント名のリストを作成
/*!
	リストに追加するのは、カテゴリがkLVstAudioEffectClassのもののみ。
*/
void SelectComponentDialog::InitializeList()
{
	static const std::wstring kLVstAudioEffectClass = L"Audio Module Class";
	component_list_.clear();
	for(int i = 0; i < factory_->GetComponentCount(); ++i) {
		if(factory_->GetComponentInfo(i).category() != kLVstAudioEffectClass) {
			continue;
		}

		auto const &info = factory_->GetComponentInfo(i);
		auto name = info.name();
		if(info.is_classinfo2_enabled()) {
			name = L"[" + info.classinfo2().sub_categories() + L"] " + name;
		}
		component_list_.add(name);
		balor::UniqueAny component_index;
		component_index = std::move(int(i));
		component_list_.setItemData(component_list_.count() - 1, std::move(component_index));
	}
	component_list_.selectedIndex(0);
}

void SelectComponentDialog::OnOk()
{
	frame_.close();
	int const component_index = 
		component_list_.getItemData<int>(
			component_list_.selectedIndex()
			);
	callback_(std::move(factory_), component_index, std::move(host_context_));
}

void SelectComponentDialog::OnCancel()
{
	frame_.close();
}

} // ::hwm