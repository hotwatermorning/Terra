#include "Sequence.hpp"
#include "../misc/Round.hpp"

NS_HWM_BEGIN

std::vector<ProcessInfo::MidiMessage>
Sequence::Cache(IMusicalTimeService const *mt)
{
    std::vector<ProcessInfo::MidiMessage> buf;
    
    using namespace MidiDataType;
    
    for(auto const &ev: notes_) {
        auto const smp_begin = Round<SampleCount>(mt->TickToSample(ev.pos_));
        auto const smp_end = Round<SampleCount>(mt->TickToSample(ev.GetEndPos()));
        
        ProcessInfo::MidiMessage msg;
        
        msg.channel_ = ev.channel_;
        msg.offset_ = smp_begin;
        msg.ppq_pos_ = mt->TickToPPQ(ev.pos_);
        msg.data_ = NoteOn { ev.pitch_, ev.velocity_ };
        buf.push_back(msg);
        
        msg.offset_ = smp_end;
        msg.ppq_pos_ = mt->TickToPPQ(ev.GetEndPos());
        msg.data_ = NoteOff { ev.pitch_, ev.off_velocity_ };
        buf.push_back(msg);
    }
    
    std::stable_sort(buf.begin(), buf.end(),
                     [](auto const &x, auto const &y) { return x.offset_ < y.offset_; }
                     );
    return buf;
}

NS_HWM_END
