#pragma once

#include <memory>
#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

class IMainFrame
:   public wxFrame
,   public SingleInstance<IMainFrame>
{
protected:
    IMainFrame();
};

IMainFrame * CreateMainFrame();

NS_HWM_END
