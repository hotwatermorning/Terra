#import <Foundation/Foundation.h>
#include "../../misc/Module.hpp"

NS_HWM_BEGIN

using InitDll = void(*)();
using ExitDll = void(*)();
using BundleEntry = bool(*)(CFBundleRef);
using BundleExit = void(*)();

Module LoadAndInitializeModule(String path)
{
    auto mod = Module(path.c_str());
    if(!mod) { return Module(); }
    
    if(auto bundle_entry = (BundleEntry)mod.get_proc_address("bundleEntry")) {
        bool const successful = bundle_entry(mod.get_native_handle());
        if(successful == false) { return Module(); }
    }
    
    if(auto init_dll = (InitDll)mod.get_proc_address("InitDll")) {
        hwm::dout << "This plugin has windows export function: InitDll()" << std::endl;
        init_dll();
    }
    
    return mod;
}

void TerminateAndReleaseModule(Module &mod)
{
    if(!mod) { return; }
    
    if(auto exit_dll = (InitDll)mod.get_proc_address("ExitDll")) {
        hwm::dout << "This plugin has windows export function: ExitDll()" << std::endl;
        exit_dll();
    }
    
    if(auto bundle_exit = (BundleExit)mod.get_proc_address("bundleExit")) {
        bundle_exit();
    }
    
    mod.reset();
}

NS_HWM_END
