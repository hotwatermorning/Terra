#include "Sequence.hpp"
#include "../misc/MathUtil.hpp"

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
        
        msg.channel_ = channel_;
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

std::unique_ptr<schema::Sequence> Sequence::ToSchema() const
{
    auto p = std::make_unique<schema::Sequence>();
    
    for(auto const &note: notes_) {
        auto new_note = p->add_notes();
        new_note->set_pos(note.pos_);
        new_note->set_length(note.length_);
        new_note->set_pitch(note.pitch_);
        new_note->set_velocity(note.velocity_);
        new_note->set_off_velocity(note.off_velocity_);
    }
    
    p->set_channel(channel_);
    
    return p;
}

std::unique_ptr<Sequence> Sequence::FromSchema(schema::Sequence const &schema)
{
    auto seq = std::make_unique<Sequence>();
    
    seq->channel_ = schema.channel();
    for(auto const &note: schema.notes()) {
        seq->notes_.push_back(Note {
            std::max<Int32>(0, note.pos()),
            std::max<Int32>(0, note.length()),
            Clamp<UInt8>(note.pitch(), 0, 127),
            Clamp<UInt8>(note.velocity(), 1, 127),
            Clamp<UInt8>(note.off_velocity(), 0, 127)
        });
    }
    
    return seq;
}

NS_HWM_END
