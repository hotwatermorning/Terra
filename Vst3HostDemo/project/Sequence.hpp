#pragma once
#include <vector>

NS_HWM_BEGIN

struct Sequence
{
    struct Note {
        SampleCount pos_ = 0;
        SampleCount length_ = 0;
        UInt8 channel_ = 0;
        UInt8 pitch_ = 0;
        UInt8 velocity_ = 0;
        UInt8 off_velocity_ = 0;
        
        Note() = default;
        Note(SampleCount pos,
             SampleCount length,
             UInt8 channel,
             UInt8 pitch,
             UInt8 velocity,
             UInt8 off_velocity = 0)
        :   pos_(pos)
        ,   length_(length)
        ,   channel_(channel)
        ,   pitch_(pitch)
        ,   velocity_(velocity)
        ,   off_velocity_(off_velocity)
        {}
        
        SampleCount GetEndPos() const { return pos_ + length_; }
    };
    
    Sequence(std::vector<Note> notes)
    : notes_(std::move(notes))
    {}
    
    std::vector<Note> notes_;
};

NS_HWM_END

