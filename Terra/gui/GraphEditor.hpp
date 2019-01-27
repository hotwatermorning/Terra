#pragma once

#include "../project/GraphProcessor.hpp"
#include <memory>

NS_HWM_BEGIN

std::unique_ptr<wxPanel> CreateGraphEditorComponent(wxWindow *parent, GraphProcessor &graph);

NS_HWM_END
