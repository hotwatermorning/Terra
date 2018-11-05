#pragma once

#if defined(_MSC_VER)
#include <windows.h>
#else
#include <cstdlib>
#include <cwchar>
#include <vector>

NS_HWM_BEGIN

void * load_impl(char const *path);
void unload_impl(void *handle);
void * get_proc_address(void *bundle, char const *function_name);

#endif

struct Module
{
#if defined(_MSC_VER)
    typedef HMODULE platform_module_type;
    
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
    typedef    void *    platform_module_type;

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
    typedef platform_module_type    module_type;
    
    Module() {}
    
    explicit
    Module(char const *path)
    :    module_(load_impl(path))
    {}
    
    explicit
    Module(wchar_t const *path)
    :    module_(load_impl(path))
    {}
    
    Module(module_type module)
    :    module_(module)
    {}
    
    Module(Module &&r)
    :    module_(std::exchange(r.module_, platform_module_type()))
    {
        r.module_ = nullptr;
    }
    
    Module & operator=(Module &&r)
    {
        module_ = std::exchange(r.module_, platform_module_type());
        return *this;
    }

    void * get_proc_address(char const *function_name) const
    {
        return get_proc_address_impl(module_, function_name);
    }

    
    ~Module()
    {
        if(module_) { unload_impl(module_); }
    }
    
public:
    module_type get() { return module_; }
    module_type get() const { return module_; }
    module_type release() { return std::exchange(module_, platform_module_type()); }
    
    template<class... Args>
    void reset(Args&&... args) { *this = Module(args...); }
    
    explicit
    operator bool() const { return module_ != platform_module_type(); }
    
private:
    module_type    module_ = platform_module_type();
};

NS_HWM_END
