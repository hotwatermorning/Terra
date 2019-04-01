// VST3 SDK中のサンプル実装などを取り込み

#include "public.sdk/source/common/memorystream.cpp"
#include "public.sdk/source/vst/hosting/eventlist.cpp"
#include "public.sdk/source/vst/hosting/hostclasses.cpp"
#include "public.sdk/source/vst/hosting/parameterchanges.cpp"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.cpp"

#include "public.sdk/source/vst/hosting/stringconvert.cpp"
#include "public.sdk/source/vst/hosting/module.cpp"
#if SMTG_OS_WINDOWS
#include "public.sdk/source/vst/hosting/module_win32.cpp"
#elif SMTG_OS_LINUX
#include "public.sdk/source/vst/hosting/module_linux.cpp"
#endif
