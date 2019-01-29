#pragma once

#include "../project/GraphProcessor.hpp"
#include <memory>

NS_HWM_BEGIN

class GraphEditor
:   public wxPanel
{
protected:
    GraphEditor(wxWindow *parent);
    
public:
    virtual
    ~GraphEditor();
    
    virtual
    void RearrangeNodes() = 0;
};

std::unique_ptr<GraphEditor> CreateGraphEditorComponent(wxWindow *parent, GraphProcessor &graph);

NS_HWM_END
