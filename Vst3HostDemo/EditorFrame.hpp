#pragma once

#include <balor/gui/all.hpp>
#include <Windows.h>
#include "vst3/pluginterfaces/gui/iplugview.h"

using namespace Steinberg;

namespace hwm {

class EditorFrame
	:	public IPlugFrame
{
public:
	EditorFrame();

public:
	DECLARE_FUNKNOWN_METHODS

public:
	void owner(balor::gui::Frame *frame);
	tresult PLUGIN_API resizeView (IPlugView* incomingView, ViewRect* newSize) override;
	void	resizeView(ViewRect const &new_size);

	HWND	GetHandle();
	bool	visible() const;
	void    visible(bool flag);

private:
	balor::gui::Frame	frame_;
	balor::gui::Panel	panel_;
};

}	// hwm