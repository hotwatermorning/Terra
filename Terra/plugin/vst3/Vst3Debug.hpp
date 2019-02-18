#pragma once

#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstunits.h>
#include "./Vst3Utils.hpp"

NS_HWM_BEGIN

void OutputParameterInfo(Steinberg::Vst::IEditController *edit_controller);
void OutputUnitInfo(Steinberg::Vst::IUnitInfo *unit_handler);
void OutputBusInfo(Steinberg::Vst::IComponent *component,
                   Steinberg::Vst::IEditController *edit_controller,
                   Steinberg::Vst::IUnitInfo *unit_handler);

std::string tresult_to_string(Steinberg::tresult result);
std::wstring tresult_to_wstring(Steinberg::tresult result);

//! resultがkResultOk以外ならエラーメッセージを出力する。
Steinberg::tresult ShowError(Steinberg::tresult result, String context);

NS_HWM_END
