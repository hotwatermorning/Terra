#pragma once

#include "../project/GraphProcessor.hpp"
#include <memory>

NS_HWM_BEGIN

class IGraphEditor
:   public wxWindow
{
protected:
    template<class... Args>
    IGraphEditor(Args&&...);

public:
    virtual
    ~IGraphEditor();

    virtual
    void RearrangeNodes() = 0;
};

std::unique_ptr<IGraphEditor> CreateGraphEditor2Component(wxWindow *parent, GraphProcessor &graph);

NS_HWM_END
