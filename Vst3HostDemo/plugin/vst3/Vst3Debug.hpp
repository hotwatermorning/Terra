#pragma once

#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstunits.h>

NS_HWM_BEGIN

void OutputUnitInfo(Steinberg::Vst::IUnitInfo *unit_handler);
void OutputBusInfo(Steinberg::Vst::IComponent *component,
                   Steinberg::Vst::IEditController *edit_controller,
                   Steinberg::Vst::IUnitInfo *unit_handler);

void showErrorMsg(Steinberg::tresult result);

NS_HWM_END
