#include "Sequence.hpp"
#include "../misc/MathUtil.hpp"
#include "../misc/StrCnv.hpp"

NS_HWM_BEGIN

bool Sequence::NoteCmp::operator()(Sequence::NotePtr const &lhs,
                                   Sequence::NotePtr const &rhs) const
{
    assert(lhs);
    assert(rhs);
    
    return lhs->pos_ < rhs->pos_;
};

void Sequence::InsertSorted(NotePtr note)
{
    assert(note);
    
    auto it = std::upper_bound(notes_.begin(),
                               notes_.end(),
                               note,
                               NoteCmp{});
    
    notes_.insert(it, note);
}

void Sequence::PushBack(NotePtr note)
{
    assert(note);
    notes_.push_back(note);
}

Sequence::NotePtr Sequence::Erase(UInt32 index)
{
    assert(index < notes_.size());
    auto p = notes_[index];
    notes_.erase(notes_.begin() + index);
    
    return p;
}

void Sequence::SortStable()
{
    std::stable_sort(notes_.begin(), notes_.end(), NoteCmp{});
}

bool Sequence::IsSorted() const
{
    return std::is_sorted(notes_.begin(), notes_.end(), NoteCmp{});
}

std::vector<ProcessInfo::MidiMessage>
Sequence::MakeCache(IMusicalTimeService const *mt) const
{
    std::vector<ProcessInfo::MidiMessage> buf;
    
    using namespace MidiDataType;
    
    for(auto const &ev: notes_) {
        auto const smp_begin = Round<SampleCount>(mt->TickToSample(ev->pos_));
        auto const smp_end = Round<SampleCount>(mt->TickToSample(ev->GetEndPos()));
        
        ProcessInfo::MidiMessage msg;
        
        msg.channel_ = channel_;
        msg.offset_ = smp_begin;
        msg.ppq_pos_ = mt->TickToPPQ(ev->pos_);
        msg.data_ = NoteOn { ev->pitch_, ev->velocity_ };
        buf.push_back(msg);
        
        msg.offset_ = smp_end;
        msg.ppq_pos_ = mt->TickToPPQ(ev->GetEndPos());
        msg.data_ = NoteOff { ev->pitch_, ev->off_velocity_ };
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
        new_note->set_pos(note->pos_);
        new_note->set_length(note->length_);
        new_note->set_pitch(note->pitch_);
        new_note->set_velocity(note->velocity_);
        new_note->set_off_velocity(note->off_velocity_);
    }
    
    p->set_channel(channel_);
    p->set_name(to_utf8(name_));
    
    return p;
}

std::unique_ptr<Sequence> Sequence::FromSchema(schema::Sequence const &schema)
{
    auto seq = std::make_unique<Sequence>();
    
    seq->name_ = to_wstr(schema.name());
    seq->channel_ = schema.channel();
    for(auto const &note: schema.notes()) {
        seq->notes_.push_back(std::make_unique<Note>(
            std::max<Int32>(0, note.pos()),
            std::max<Int32>(0, note.length()),
            Clamp<UInt8>(note.pitch(), 0, 127),
            Clamp<UInt8>(note.velocity(), 1, 127),
            Clamp<UInt8>(note.off_velocity(), 0, 127)
        ));
    }
    
    return seq;
}

NS_HWM_END
