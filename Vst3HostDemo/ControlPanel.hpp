#pragma once

#include <iomanip>
#include <sstream>

#include <boost/function.hpp>
#include <balor/gui/all.hpp>
#include "./PeakMeterDisplay.hpp"
#include "./namespace.hpp"

namespace hwm {

class ControlPanel
{
public:
	typedef boost::function<void(size_t index)> program_select_handler_t;
	typedef boost::function<void(balor::String module_path)> plugin_select_handler_t;
	typedef boost::function<void(double volume_dB)> volume_change_handler_t;
	typedef PeakMeterDisplay::peak_level_query_handler_t peak_level_query_handler_t;

private:
	gui::Panel		wnd_;
	gui::Label		plugin_name_;
	gui::Label		program_list_label_;
	gui::ComboBox	program_list_;
	gui::Button		open_plugin_;
	gui::Button		close_plugin_;
	gui::Button		save_bank_;
	gui::Button		load_bank_;
	gui::Label		volume_label_;
	gui::TrackBar	volume_fader_;
	gpx::Font		font_;
	gpx::Font		font_small_;
	PeakMeterDisplay peak_meter_;
	program_select_handler_t	program_select_handler_;
	plugin_select_handler_t		plugin_select_handler_;
	volume_change_handler_t		volume_change_handler_;

public:
	ControlPanel(gui::Control &parent, int x, int y, int width, int height)
		:	wnd_(parent, x, y, width, height)
		,	plugin_name_(wnd_, 10, 10, 200, 27, L"")
		,	program_list_label_(wnd_, 10, 80, 200, 18, L"")
		,	program_list_(wnd_, 10, 100, 200, 20, gui::ComboBox::Style::dropDownList)
		,	open_plugin_(wnd_, 210, 10, 50, 27, L"Open")
		,	font_(L"メイリオ", 18, gpx::Font::Style::regular, gpx::Font::Quality::default)
		,	font_small_(L"メイリオ", 12, gpx::Font::Style::regular, gpx::Font::Quality::default)
		,	peak_meter_(wnd_, width - 120, 30, 30, height - 40)
		,	volume_label_(wnd_, width - 60, 10, 60, 20, L"N/A")
		,	volume_fader_(wnd_, width - 60, 30, 60, height - 30)
	{
		plugin_name_.font(font_);
		plugin_name_.edge(gui::Control::Edge::sunken);

		wnd_.onMouseDown() = [this] (gui::Panel::MouseDown &) {
			wnd_.focus();
		};

		program_list_label_.edge(gui::Control::Edge::none);
		program_list_label_.font(font_small_);
		program_list_.list().font(font_small_);
		program_list_label_.text(L"Program List");

		open_plugin_.onClick() = [this] (gui::Button::Click &) { OnClick(); };

		volume_label_.edge(gui::Control::Edge::sunken);
		volume_label_.font(font_small_);

		volume_fader_.tickVisible(true);
		volume_fader_.vertical(true);  
		volume_fader_.maximum(960);  // -96.0dB
		volume_fader_.minimum(-120); // +12.0dB

		volume_fader_.clearTick();
		
		for(int i = -120; i < 960; i += 120) {
			volume_fader_.setTick(i);
		}
		//+/-6.0dB位置にも目盛設定
		volume_fader_.setTick(60); 
		volume_fader_.setTick(-60);

		volume_fader_.onValueChange() = [this](gui::TrackBar::ValueChange &e) {
			OnValueChange(e);
		};
	}

	struct IPluginInfo
	{
		virtual balor::String	GetName() const = 0;
		virtual size_t			GetProgramCount() const = 0;
		virtual balor::String	GetProgramName(size_t n) const = 0;

	protected:
		IPluginInfo() {};

	public:
		virtual ~IPluginInfo() {}
	};

	void SetPluginInfo(IPluginInfo const &info)
	{
		BOOST_ASSERT(&info);

		plugin_name_.text(info.GetName());

		std::vector<std::wstring> program_names(info.GetProgramCount());
		for(size_t i = 0; i < info.GetProgramCount(); ++i) {
			program_names[i] = info.GetProgramName(i);
		}

		program_list_.list().items(program_names);

		program_list_.onSelect() = [&] (gui::ComboBox::Select &e) {
			int const selected = e.sender().selectedIndex();
			if(selected != -1) {
				program_select_handler_(selected);
			}
		};
	}

	void ClearPluginInfo()
	{
		plugin_name_.text(L"");
		program_list_.list().clear();
		program_list_.list().selectedIndex(-1);
		// リストの選択状態が残ってしまうので再描画
		program_list_.invalidate();
	}

	void SetNumChannels(size_t num_channels)
	{
		peak_meter_.SetNumChannels(num_channels);
	}

public:
	void SetProgramSelectHandler(program_select_handler_t handler)
	{
		program_select_handler_ = handler;
	}

	void SetPluginSelectHandler(plugin_select_handler_t handler)
	{
		plugin_select_handler_ = handler;
	}

	void SetVolumeChangeHandler(volume_change_handler_t handler)
	{
		volume_change_handler_ = handler;
		volume_fader_.value(1);
		volume_fader_.value(0);
	}

	void SetQueryLevelHandler(peak_level_query_handler_t handler)
	{
		peak_meter_.SetQueryLevelHandler(handler);
	}

private:
	void OnClick()
	{
		gui::OpenFileDialog  file_dialog;
		file_dialog.pathMustExist(true);
		file_dialog.filter(L"VST3 Module(*.vst3)\n*.vst3\nAll Files(*.*)\n*.*\n\n");
		file_dialog.title(L"Select a VSTi DLL");
		bool selected = file_dialog.show(wnd_);
		if(!selected) { return; }

		plugin_select_handler_(file_dialog.filePath());
	}

	void OnValueChange(gui::TrackBar::ValueChange &e)
	{
		double const dB = e.newValue() / -10.0;
		volume_change_handler_(dB);
		std::wstringstream ss;
		ss << std::right << std::showpos << std::fixed << std::setprecision(1) << dB << L"dB";
		volume_label_.text(ss.str());
	}
};

} //::hwm