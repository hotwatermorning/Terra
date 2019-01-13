#include "./UnitData.hpp"

NS_HWM_BEGIN

std::vector<Vst3Plugin::UnitInfo> GetSelectableUnitInfos(Vst3Plugin *plugin)
{
    std::vector<Vst3Plugin::UnitInfo> ret;
    
    auto const num = plugin->GetNumUnitInfo();
    for(int un = 0; un < num; ++un) {
        auto const &info = plugin->GetUnitInfoByIndex(un);
        auto const &pl = info.program_list_;
        if(info.program_change_param_ == Steinberg::Vst::kNoParamId) { continue; }
        if(pl.id_ == Steinberg::Vst::kNoProgramListId) { continue; }
        if(pl.programs_.empty()) { continue; }
        
        ret.push_back(info);
    }
    
    return ret;
}

Vst3Plugin::UnitID GetUnitID(wxChoice const *choice)
{
    auto const sel = choice->GetSelection();
    if(sel == wxNOT_FOUND) { return -1; }
    
    assert(choice->HasClientObjectData());
    auto unit_data = dynamic_cast<UnitData const *>(choice->GetClientObject(sel));
    if(!unit_data) { return -1; }
    
    return unit_data->unit_id_;
}

NS_HWM_END
