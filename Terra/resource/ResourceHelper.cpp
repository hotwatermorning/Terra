#include "ResourceHelper.hpp"

#include <wx/stdpaths.h>
#include <wx/filename.h>

NS_HWM_BEGIN

ResourceHelper::ResourceHelper()
{}

ResourceHelper::~ResourceHelper()
{}

String ResourceHelper::GetTerraDir() const
{
    auto &sp = wxStandardPaths::Get();
    auto path = wxFileName(sp.GetDocumentsDir(), "");
    path.AppendDir("diatonic.jp");
    path.AppendDir("Terra");
    return path.GetFullPath();
}

String ResourceHelper::GetConfigFilePath() const
{
    auto path = wxFileName(GetTerraDir(), "");
    path.AppendDir("Config");
    path.AppendDir("terra.conf");
    return path.GetFullPath();
}

//! Get resource file path specified by the path hierarchy.
String ResourceHelper::GetResourcePath(String path) const
{
    assert(path.size() > 0);
    
    if(path.front() != L'/' && path.front() != L'\\') {
        path = L'/' + path;
    }
    
    return wxStandardPaths::Get().GetResourcesDir() + path;
}

//! Get resource file path specified by the path hierarchy.
String ResourceHelper::GetResourcePath(std::vector<String> path_hierarchy) const
{
    assert(path_hierarchy.empty() == false &&
           std::all_of(path_hierarchy.begin(),
                       path_hierarchy.end(),
                       [](auto const &x) { return x.size() > 0; }
                       ));
    
    String concat;
    
    for(auto x: path_hierarchy) {
        if(x.front() != L'/' && x.front() != L'\\') {
            x = L'/' + x;
        }
        if(x.back() == L'/' || x.back() == L'\\') {
            x.pop_back();
        }
        
        concat += x;
    }
    
    return GetResourcePath(concat);
}

NS_HWM_END
