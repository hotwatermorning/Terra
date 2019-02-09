#pragma once

#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

class ResourceHelper
:   public SingleInstance<ResourceHelper>
{
public:
    ResourceHelper();
    ~ResourceHelper();
    
    //! Get resource file path specified by the path hierarchy.
    String GetResourcePath(String path) const;
    
    //! Get resource file path specified by the path hierarchy.
    String GetResourcePath(std::vector<String> path_hierarchy) const;
    
    template<class T>
    T GetResourceAs(String path) const
    {
        return T(GetResourcePath(path));
    }
    
    template<class T>
    T GetResourceAs(std::vector<String> path_hierarchy) const
    {
        return T(GetResourcePath(path_hierarchy));
    }
};

inline
String GetResourcePath(String path)
{
    return ResourceHelper::GetInstance()->GetResourcePath(path);
}

inline
String GetResourcePath(std::vector<String> path_hierarchy)
{
    return ResourceHelper::GetInstance()->GetResourcePath(path_hierarchy);
}

template<class T>
T GetResourceAs(String path)
{
    return ResourceHelper::GetInstance()->GetResourceAs<T>(path);
}

template<class T>
T GetResourceAs(std::vector<String> path_hierarchy)
{
    return ResourceHelper::GetInstance()->GetResourceAs<T>(path_hierarchy);
}

NS_HWM_END
