#pragma once

#include <functional>
#include "../plugin/vst3/Vst3Plugin.hpp"

NS_HWM_BEGIN

wxFrame * CreatePluginEditorFrame(wxWindow *parent,
                                  Vst3Plugin *target_plugin,
                                  std::function<void()> on_destroy);

NS_HWM_END
