#pragma once
#include <vector>

#include "./IMusicalTimeService.hpp"
#include "../processor/ProcessInfo.hpp"
#include <project.pb.h>


NS_HWM_BEGIN

struct Sequence
{
    struct Note {
        enum class SelectionState {
            kNeutral,
            kSelected,
            kCovered,
        };
        
        SelectionState GetSelectionState() const { return sel_; }
        void SetSelectionState(SelectionState sel) { sel_ = sel; }
        
        bool IsNeutral() const { return sel_ == SelectionState::kNeutral; }
        bool IsSelected() const { return sel_ == SelectionState::kSelected; }
        bool IsCovered() const { return sel_ == SelectionState::kCovered; }
        void SetNeutral() { sel_ = SelectionState::kNeutral; }
        void SetSelected() { sel_ = SelectionState::kSelected; }
        void SetCovered() { sel_ = SelectionState::kCovered; }
        
        Tick pos_ = 0;
        Tick length_ = 0;
        UInt8 pitch_ = 0;
        
        Tick prev_pos_ = 0;
        Tick prev_length_ = 0;
        UInt8 prev_pitch_ = 0;
        SelectionState sel_ = SelectionState::kNeutral;
        
        UInt8 velocity_ = 0;
        UInt8 off_velocity_ = 0;
        
        Note() = default;
        Note(Tick pos,
             Tick length,
             UInt8 pitch,
             UInt8 velocity = 64,
             UInt8 off_velocity = 0)
        :   pos_(pos)
        ,   length_(length)
        ,   pitch_(pitch)
        ,   velocity_(velocity)
        ,   off_velocity_(off_velocity)
        ,   prev_pos_(pos)
        ,   prev_length_(length)
        ,   prev_pitch_(pitch)
        {}
        
        Tick GetEndPos() const { return pos_ + length_; }
        Tick GetPrevEndPos() const { return prev_pos_ + prev_length_; }
        void ClearPrevState() {
            prev_pos_ = pos_;
            prev_length_ = length_;
            prev_pitch_ = pitch_;
        }
    };
    
    using NotePtr = std::shared_ptr<Note>;
    
    struct NoteCmp
    {
        bool operator()(Sequence::NotePtr const &lhs, Sequence::NotePtr const &rhs) const;
    };
    
    Sequence()
    {}
    
    explicit
    Sequence(String name, std::vector<std::shared_ptr<Note>> notes = {}, UInt32 channel = 0)
    :   name_(name)
    ,   notes_(std::move(notes))
    ,   channel_(channel)
    {}
    
    Sequence(String name, std::vector<Note> notes, UInt32 channel = 0)
    :   name_(name)
    ,   channel_(channel)
    {
        for(auto n: notes) {
            notes_.push_back(std::make_shared<Note>(n));
        }
    }
    
    Sequence(Sequence const &) = delete;
    Sequence & operator=(Sequence const &) = delete;
    Sequence(Sequence &&) = default;
    Sequence & operator=(Sequence &&) = default;
    
    virtual ~Sequence()
    {}
    
    String name_;
    std::vector<NotePtr> notes_;
    UInt8 channel_ = 0;
    
    //! insert note at sorted
    void InsertSorted(NotePtr note);
    void PushBack(NotePtr note);
    NotePtr Erase(UInt32 index);
    void SortStable();
    bool IsSorted() const;
    
    std::vector<ProcessInfo::MidiMessage> MakeCache(IMusicalTimeService const *conv) const;
    
    std::unique_ptr<schema::Sequence> ToSchema() const;
    static
    std::unique_ptr<Sequence> FromSchema(schema::Sequence const &seq);
};

using SequencePtr = std::shared_ptr<Sequence>;

NS_HWM_END

