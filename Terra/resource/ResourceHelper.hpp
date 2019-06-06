#pragma once

#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

//! Get the root dir of Terra
/*! "~/Documents/diatonic.jp/Terra"
 */
String GetTerraDir();

//! Get the config file path.
/*! "~/Documents/diatonic.jp/Terra/Config/terra.conf"
 */
String GetConfigFilePath();

//! Get resource file path specified by the path hierarchy.
String GetResourcePath(String path);

//! Get resource file path specified by the path hierarchy.
String GetResourcePath(std::vector<String> path_hierarchy);

template<class T>
T GetResourceAs(String path)
{
    return T(GetResourcePath(path));
}

template<class T>
T GetResourceAs(std::vector<String> path_hierarchy)
{
    return T(GetResourcePath(path_hierarchy));
}

NS_HWM_END
