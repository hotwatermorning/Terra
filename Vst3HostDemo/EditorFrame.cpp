#include <balor/gui/all.hpp>
#include <balor/graphics/all.hpp>

#include "EditorFrame.hpp"
#include "namespace.hpp"

namespace hwm {

IMPLEMENT_REFCOUNT(EditorFrame)

EditorFrame::EditorFrame()
:	frame_(L"Editor Frame")
,	panel_(frame_, 0, 0, 400, 300)
{
	frame_.icon(gpx::Icon::windowsLogo());
	frame_.maximizeButton(false);
	frame_.onClosing() = [](gui::Frame::Closing &e) {
		e.cancel(true);
		e.sender().minimized(true);
	};
}

tresult EditorFrame::queryInterface(char const *iid, void **obj)
{
	QUERY_INTERFACE(iid, obj, Steinberg::FUnknown::iid, IPlugFrame);
	QUERY_INTERFACE(iid, obj, Steinberg::IPlugFrame::iid, IPlugFrame);
	*obj = 0;
	return kNoInterface;
}

void EditorFrame::owner(balor::gui::Frame *owner)
{
	frame_.owner(owner);
}

tresult PLUGIN_API EditorFrame::resizeView (IPlugView* incomingView, ViewRect* newSize)
{
	resizeView(*newSize);
	return kResultOk;
}

void EditorFrame::resizeView (ViewRect const &newSize)
{
	frame_.clientSize(
		balor::Size(newSize.getWidth(), newSize.getHeight())
		);

	panel_.size(
		balor::Size(newSize.getWidth(), newSize.getHeight())
		);

	frame_.size(
		frame_.sizeFromClientSize(frame_.clientSize())
		);
}

HWND EditorFrame::GetHandle() 
{
	return (HWND)panel_;
}

bool EditorFrame::visible() const
{
	return frame_.visible() && panel_.visible();
}

void EditorFrame::visible(bool flag)
{
	frame_.visible(flag);
	panel_.visible(flag);
}

}	// hwm