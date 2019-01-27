#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>

NS_HWM_BEGIN

void * GetWindowRef(NSView *view)
{
    auto wnd = view.window;
    return wnd.windowRef;
}

NS_HWM_END
