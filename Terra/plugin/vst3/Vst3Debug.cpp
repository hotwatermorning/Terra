#include "Vst3Debug.hpp"

#include <iomanip>
#include <sstream>
#include <pluginterfaces/vst/vstpresetkeys.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include "../../misc/StrCnv.hpp"
#include "./Vst3Utils.hpp"

using namespace Steinberg;

NS_HWM_BEGIN

std::string tresult_to_string(tresult result)
{
    switch(result) {
        case kResultOk: return "successful";
        case kNoInterface: return "no Interface";
        case kResultFalse: return "result false";
        case kInvalidArgument: return "invalid argument";
        case kNotImplemented: return "not implemented";
        case kInternalError: return "internal error";
        case kNotInitialized: return "not initialized";
        case kOutOfMemory: return "out of memory";
        default: return "unknown error";
    }
}

std::wstring tresult_to_wstring(tresult result)
{
    return to_wstr(tresult_to_string(result));
}

Steinberg::tresult ShowError(Steinberg::tresult result, String context)
{
    if(result != kResultOk) {
        hwm::wdout << L"Failed({}): {}"_format(tresult_to_wstring(result), context) << std::endl;
    }
    
    return result;
}

void OutputParameterInfo(Vst::IEditController *edit_controller)
{
    hwm::wdout << "--- Output Parameter Info ---" << std::endl;
    
    UInt32 const num = edit_controller->getParameterCount();
    
    for(UInt32 i = 0; i < num; ++i) {
        Vst::ParameterInfo info;
        edit_controller->getParameterInfo(i, info);
        
        hwm::wdout <<
        L"{}: {{"
        L"ID:{}, Title:{}, ShortTitle:{}, Units:{}, "
        L"StepCount:{}, Default:{}, UnitID:{}, "
        L"CanAutomate:{}, "
        L"IsReadOnly:{}, "
        L"IsWrapAround:{}, "
        L"IsList:{}, "
        L"IsProgramChange:{}, "
        L"IsByPass:{}"
        L"}}"_format(i, info.id, to_wstr(info.title), to_wstr(info.shortTitle), to_wstr(info.units),
                     info.stepCount, info.defaultNormalizedValue, info.unitId,
                     (info.flags & info.kCanAutomate) != 0,
                     (info.flags & info.kIsReadOnly) != 0,
                     (info.flags & info.kIsWrapAround) != 0,
                     (info.flags & info.kIsList) != 0,
                     (info.flags & info.kIsProgramChange) != 0,
                     (info.flags & info.kIsBypass) != 0
                     )
        << std::endl;
    }
}

String UnitInfoToString(Vst::UnitInfo const &info)
{
    return
    L"ID: {}, Name: {}, Parent: {}, Program List ID: {}"_format(info.id,
                                                                to_wstr(info.name),
                                                                info.parentUnitId,
                                                                info.programListId);
}

String ProgramListInfoToString(Vst::ProgramListInfo const &info)
{
    return
    L"ID: {}, Name: {}, Program Count: {}"_format(info.id,
                                                  to_wstr(info.name),
                                                  info.programCount);
}

void OutputUnitInfo(Vst::IUnitInfo *unit_handler)
{
    assert(unit_handler);
    
    hwm::wdout << "--- Output Unit Info ---" << std::endl;
    
    for(size_t i = 0; i < unit_handler->getUnitCount(); ++i) {
        Vst::UnitInfo unit_info {};
        unit_handler->getUnitInfo(i, unit_info);
        hwm::wdout << L"[" << i << L"] " << UnitInfoToString(unit_info) << std::endl;
    }
    
    hwm::wdout << L"Selected Unit: " << unit_handler->getSelectedUnit() << std::endl;
    
    hwm::wdout << "--- Output Program List Info ---" << std::endl;
    
    for(size_t i = 0; i < unit_handler->getProgramListCount(); ++i) {
        Vst::ProgramListInfo program_list_info {};
        tresult res = unit_handler->getProgramListInfo(i, program_list_info);
        if(res != kResultOk) {
            hwm::wdout << "Getting program list info failed." << std::endl;
            break;
        }
        
        hwm::wdout << L"[{}] {}"_format(i, ProgramListInfoToString(program_list_info)) << std::endl;
        
        for(size_t program_index = 0; program_index < program_list_info.programCount; ++program_index) {
            
            hwm::wdout << L"\t[{}] "_format(program_index);
            
            Vst::String128 name;
            unit_handler->getProgramName(program_list_info.id, program_index, name);
            
            hwm::wdout << to_wstr(name);
            
            Vst::CString attrs[] {
                Vst::PresetAttributes::kPlugInName,
                Vst::PresetAttributes::kPlugInCategory,
                Vst::PresetAttributes::kInstrument,
                Vst::PresetAttributes::kStyle,
                Vst::PresetAttributes::kCharacter,
                Vst::PresetAttributes::kStateType,
                Vst::PresetAttributes::kFilePathStringType,
                Vst::PresetAttributes::kFileName
            };
            
            for(auto attr: attrs) {
                Vst::String128 attr_value = {};
                unit_handler->getProgramInfo(program_list_info.id, program_index, attr, attr_value);
                
                hwm::wdout << L", {}: {}"_format(to_wstr(attr), to_wstr(attr_value));
            }
            
            if(unit_handler->hasProgramPitchNames(program_list_info.id, program_index) == kResultTrue) {
                Vst::String128 pitch_name = {};
                int16 const pitch_center = 0x2000;
                unit_handler->getProgramPitchName(program_list_info.id, program_index, pitch_center, pitch_name);
                
                hwm::wdout << L", " << to_wstr(pitch_name);
            } else {
                hwm::wdout << L", No Pitch Name";
            }
            
            hwm::wdout << std::endl;
        }
    }
}

String BusInfoToString(Vst::BusInfo &bus, Vst::SpeakerArrangement speaker)
{
    return
    L"Bus{{ Name: {}, MediaType: {}, Direction: {}, "
          L"BusType: {}, Channels: {}, Default Active: {}, "
          L"Speaker: {}"
    L" }}"_format(to_wstr(bus.name),
                  (bus.mediaType == Vst::MediaTypes::kAudio ? L"Audio" : L"Midi"),
                  (bus.direction == Vst::BusDirections::kInput ? L"Input" : L"Output"),
                  (bus.busType == Vst::BusTypes::kMain ? L"Main Bus" : L"Aux Bus"),
                  bus.channelCount,
                  ((bus.flags & bus.kDefaultActive) != 0),
                  GetSpeakerName(speaker)
                  );
}

// this function returns a string which representing relationship between a bus and units.
String BusUnitInfoToString(int bus_index, Vst::BusInfo const &bus, Vst::IUnitInfo *unit_handler, int num_spaces)
{
    auto const spaces = String(num_spaces, L' ');
    
    auto get_unit_info_of_channel = [&](int channel) -> String {
        Vst::UnitID unit_id;
        // `This method mainly is intended to find out which unit is related to a given MIDI input channel`
        tresult result = unit_handler->getUnitByBus(bus.mediaType, bus.direction, bus_index, channel, unit_id);
        if(result == kResultOk) {
            // ok
        } else if(result == kResultFalse) {
            return spaces + L"No related unit info for this bus channel.";
        } else {
            return spaces + L"Failed to get related unit info for this bus channel: " + to_wstr(tresult_to_string(result));
        }
        
        Vst::UnitInfo unit_info;
        auto const num_units = unit_handler->getUnitCount();
        for(int i = 0; i < num_units; ++i) {
            Vst::UnitInfo tmp;
            unit_handler->getUnitInfo(i, tmp);
            if(tmp.id == unit_id) {
                unit_info = tmp;
                break;
            }
            
            if(i != num_units - 1) {
                return spaces + L"No related unit info for this bus channel.";
            }
        }
        
        return spaces + L"Channel:{:#2d} => Unit: {})"_format(channel, to_wstr(unit_info.name));
    };

    String str;
    for(int ch = 0; ch < bus.channelCount; ++ch) {
        str += get_unit_info_of_channel(ch);
        if(ch != bus.channelCount-1) { str += L"\n"; }
    }
    return str;
}

void OutputBusInfoImpl(Vst::IComponent *component,
                       Vst::IUnitInfo *unit_handler,
                       Vst::MediaTypes media_type,
                       Vst::BusDirections bus_direction)
{
    int const kNumSpaces = 4;
    
    int const num_components = component->getBusCount(media_type, bus_direction);
    if(num_components == 0) {
        hwm::wdout << "No such buses for this component." << std::endl;
        return;
    }
    
    for(int i = 0; i < num_components; ++i) {
        Vst::BusInfo bus_info;
        component->getBusInfo(media_type, bus_direction, i, bus_info);
        auto ap = std::move(queryInterface<Vst::IAudioProcessor>(component).right());

        Vst::SpeakerArrangement speaker;
        ap->getBusArrangement(bus_direction, i, speaker);
        
        auto const bus_info_str = BusInfoToString(bus_info, speaker);
        auto const bus_unit_info_str
        = (unit_handler && bus_info.channelCount > 0)
        ?   BusUnitInfoToString(i, bus_info, unit_handler, kNumSpaces)
        :   String(kNumSpaces, L' ') + L"No unit info for this bus."
        ;
        
        hwm::wdout << L"[{}] {}\n{}"_format(i, bus_info_str, bus_unit_info_str) << std::endl;
    }
}

void OutputBusInfo(Vst::IComponent *component,
                   Vst::IEditController *edit_controller,
                   Vst::IUnitInfo *unit_handler)
{
    hwm::dout << "-- output bus info --" << std::endl;
    hwm::dout << "[Audio Input]" << std::endl;
    OutputBusInfoImpl(component, unit_handler, Vst::MediaTypes::kAudio, Vst::BusDirections::kInput);
    hwm::dout << "[Audio Output]" << std::endl;
    OutputBusInfoImpl(component, unit_handler, Vst::MediaTypes::kAudio, Vst::BusDirections::kOutput);
    hwm::dout << "[Event Input]" << std::endl;
    OutputBusInfoImpl(component, unit_handler, Vst::MediaTypes::kEvent, Vst::BusDirections::kInput);
    hwm::dout << "[Event Output]" << std::endl;
    OutputBusInfoImpl(component, unit_handler, Vst::MediaTypes::kEvent, Vst::BusDirections::kOutput);
}

NS_HWM_END
