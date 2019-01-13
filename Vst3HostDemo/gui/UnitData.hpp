#pragma once

#include <vector>

#include "../plugin/vst3/Vst3Plugin.hpp"

NS_HWM_BEGIN

class UnitData : public wxClientData
{
public:
    UnitData(Steinberg::Vst::UnitID unit_id)
    :   unit_id_(unit_id)
    {}
    
    Steinberg::Vst::UnitID unit_id_ = -1;
};

//! Program Listを持っているUnitInfoを返す。
std::vector<Vst3Plugin::UnitInfo> GetSelectableUnitInfos(Vst3Plugin *plugin);

Vst3Plugin::UnitID GetUnitID(wxChoice const *);

NS_HWM_END
