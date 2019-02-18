#pragma once

#if defined(_MSC_VER)
#include <windows.h>
#else
#include <Carbon/Carbon.h>
#include <cstdlib>
#include <cwchar>
#include <vector>
#endif

NS_HWM_BEGIN

class Module
{
public:
#if defined(_MSC_VER)
    using platform_module_type = HMODULE;
    
    static
    platform_module_type load_impl(const char *path) { return LoadLibraryA(path); }
    static
    platform_module_type load_impl(const wchar_t *path) { return LoadLibraryW(path); }
    static
    void unload_impl(platform_module_type handle) { assert(handle); FreeLibrary(handle); }
    static
    void * get_proc_address_impl(platform_module_type module, char const *function_name)
    {
        return GetProcAddress(module, function_name);
    }
#else
    using platform_module_type = CFBundleRef;

    static
    platform_module_type load_impl(const wchar_t *path)
    {
        auto const error_result = static_cast<std::size_t>(-1);
        
        auto const num_estimated_bytes = std::wcstombs(NULL, path, 0);
        if (error_result == num_estimated_bytes) {
            return NULL;
        }
        auto const buffer_length = (num_estimated_bytes / sizeof(char)) + 1;
        std::vector<char> tmp(buffer_length);
        std::mbstate_t state = std::mbstate_t();
        auto const num_converted_bytes = std::wcsrtombs(tmp.data(),
                                                        &path,
                                                        buffer_length * sizeof(char),
                                                        &state);
        if (error_result == num_converted_bytes) {
            return NULL;
        }
        
        return load_impl(tmp.data());
    }
    
    static
    platform_module_type load_impl(const char *path);
    static
    void unload_impl(platform_module_type handle);
    static
    void * get_proc_address_impl(platform_module_type module, char const *function_name);
#endif
    
public:
    Module() {}
    
    explicit
    Module(char const *path)
    :    module_(load_impl(path))
    {}
    
    explicit
    Module(wchar_t const *path)
    :    module_(load_impl(path))
    {}
    
    Module(platform_module_type module)
    :    module_(module)
    {}
    
    Module(Module &&r)
    :    module_(std::exchange(r.module_, platform_module_type()))
    {}
    
    Module & operator=(Module &&r)
    {
        module_ = std::exchange(r.module_, platform_module_type());
        return *this;
    }

    void * get_proc_address(char const *function_name) const
    {
        return get_proc_address_impl(module_, function_name);
    }

    platform_module_type get_native_handle() const { return module_; }
    
    ~Module()
    {
        if(module_) { unload_impl(module_); }
    }
    
public:
    platform_module_type get() { return module_; }
    platform_module_type get() const { return module_; }
    platform_module_type release() { return std::exchange(module_, platform_module_type()); }
    
    template<class... Args>
    void reset(Args&&... args) { *this = Module(args...); }
    
    explicit
    operator bool() const { return module_ != platform_module_type(); }
    
private:
    platform_module_type module_ = platform_module_type();
};

NS_HWM_END
