#import <Foundation/Foundation.h>
#import <cocoa/Cocoa.h>

NS_HWM_BEGIN

UInt32 GetMenuBarHeight()
{
    CGFloat menuBarHeight = [[[NSApplication sharedApplication] mainMenu] menuBarHeight];
    return std::round<UInt32>(menuBarHeight);
}

NS_HWM_END
