#pragma once
#include <vector>

#include "./IMusicalTimeService.hpp"
#include "../processor/ProcessInfo.hpp"
#include <project.pb.h>


NS_HWM_BEGIN

struct Sequence
{
    struct Note {
        Tick pos_ = 0;
        Tick length_ = 0;
        UInt8 pitch_ = 0;
        UInt8 velocity_ = 0;
        UInt8 off_velocity_ = 0;
        
        Note() = default;
        Note(Tick pos,
             Tick length,
             UInt8 pitch,
             UInt8 velocity,
             UInt8 off_velocity = 0)
        :   pos_(pos)
        ,   length_(length)
        ,   pitch_(pitch)
        ,   velocity_(velocity)
        ,   off_velocity_(off_velocity)
        {}
        
        Tick GetEndPos() const { return pos_ + length_; }
    };
    
    Sequence()
    {}
    
    Sequence(std::vector<Note> notes)
    : notes_(std::move(notes))
    {}
    
    std::vector<Note> notes_;
    UInt8 channel_ = 0;
    
    std::vector<ProcessInfo::MidiMessage> Cache(IMusicalTimeService const *conv);
    
    std::unique_ptr<schema::Sequence> ToSchema() const;
    static
    std::unique_ptr<Sequence> FromSchema(schema::Sequence const &seq);
};

NS_HWM_END

