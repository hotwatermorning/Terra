#import <Foundation/Foundation.h>
#import "Module.hpp"

#import <wx/wxprec.h>
#ifndef WX_PRECOMP
#import <wx/wx.h>
#endif
#import <wx/osx/core/cfstring.h>

NS_HWM_BEGIN

Module::platform_module_type Module::load_impl(char const *path)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(0,
                                                           (const UInt8*)path,
                                                           (CFIndex)std::strlen(path),
                                                           true);
    if(!url) { return nullptr; }
    
    CFBundleRef bundleRef = CFBundleCreate (kCFAllocatorDefault, url);
    CFRelease (url);
        
    if (!bundleRef) { return nullptr; }
    CFErrorRef error = nullptr;
            
    if(CFBundleLoadExecutableAndReturnError (bundleRef, &error) == false) {
        CFRelease(bundleRef);
        return nullptr;
    }
    
    if(error) {
        if (CFStringRef failureMessage = CFErrorCopyFailureReason (error))
        {
            wxCFStringRef str = failureMessage;
            hwm::dout << str.AsString().ToStdString() << std::endl;
            CFRelease (failureMessage);
        }
        
        CFRelease (error);
    }
    
    return bundleRef;    
}

void Module::unload_impl(Module::platform_module_type handle)
{
    assert(handle);
    CFRelease(handle);
}

void * Module::get_proc_address_impl(platform_module_type module, char const *function_name)
{
    CFStringRef str = CFStringCreateWithCString(kCFAllocatorDefault, function_name, kCFStringEncodingUTF7);
    void * p = CFBundleGetFunctionPointerForName((CFBundleRef)module, str);
    CFRelease(str);
    
    return p;
}

NS_HWM_END
