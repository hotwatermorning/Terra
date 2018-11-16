#pragma once

#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstunits.h>

NS_HWM_BEGIN

void OutputParameterInfo(Steinberg::Vst::IEditController *edit_controller);
void OutputUnitInfo(Steinberg::Vst::IUnitInfo *unit_handler);
void OutputBusInfo(Steinberg::Vst::IComponent *component,
                   Steinberg::Vst::IEditController *edit_controller,
                   Steinberg::Vst::IUnitInfo *unit_handler);

std::string tresult_to_string(Steinberg::tresult result);
std::wstring tresult_to_wstring(Steinberg::tresult result);

NS_HWM_END
