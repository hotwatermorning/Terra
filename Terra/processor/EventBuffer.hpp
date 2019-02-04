#pragma once

#include <cassert>
#include <algorithm>
#include <array>
#include <vector>

#include "./ProcessInfo.hpp"

NS_HWM_BEGIN

struct EventBuffer : public ProcessInfo::IEventBuffer
{
    constexpr static UInt32 kNumMIDIPitches = 128;
    constexpr static UInt32 kNumMIDIChannels = 16;
    
    EventBuffer(UInt32 num_initial_size = 2048)
    {
        events_.reserve(num_initial_size);
        std::for_each(note_stack_.begin(), note_stack_.end(), [](auto &x) { x.store(0); });
        note_off_cache_.reserve(kNumMIDIPitches);
    }
    
    EventBuffer(EventBuffer const &rhs)
    {
        events_ = rhs.events_;
        note_off_cache_ = rhs.note_off_cache_;
    }
    
    EventBuffer & operator=(EventBuffer const &rhs)
    {
        events_ = rhs.events_;
        note_off_cache_ = rhs.note_off_cache_;
        std::for_each(note_stack_.begin(), note_stack_.end(), [](auto &x) { x.store(0); });
        
        return *this;
    }
    
    void AddEvent(ProcessInfo::MidiMessage const &msg) override
    {
        if(auto p = msg.As<MidiDataType::NoteOn>()) {
            GetNoteStack(p->pitch_, msg.channel_).fetch_add(1);
        } else if(auto p = msg.As<MidiDataType::NoteOff>()) {
            auto &ref = GetNoteStack(p->pitch_, msg.channel_);
            for( ; ; ) {
                auto current = ref.load();
                if(current == 0) { break; }
                if(ref.compare_exchange_strong(current, current - 1)) {
                    break;
                }
            }
        }
        
        events_.push_back(msg);
    }
    
    ProcessInfo::MidiMessage const & GetEvent(UInt32 index) const override
    {
        assert(index < events_.size());
        return events_[index];
    }
    
    UInt32 GetCount() const override
    {
        return events_.size();
    }
    
    ArrayRef<ProcessInfo::MidiMessage const> GetRef() const override
    {
        return events_;
    }
    
    void AddEvents(ArrayRef<ProcessInfo::MidiMessage const> ref)
    {
        for(auto m: ref) {
            AddEvent(m);
        }
    }
    
    void Clear()
    {
        events_.clear();
    }
    
    void Sort()
    {
        std::stable_sort(events_.begin(), events_.end(),
                         [](auto const &x, auto const &y) {
                             return x.offset_ < y.offset_;
                         });
    }
    
    void ApplyCachedNoteOffs()
    {
        if(note_off_cache_.empty() == false) {
            events_.insert(events_.begin(), note_off_cache_.begin(), note_off_cache_.end());
            note_off_cache_.clear();
        }
    }
    
    void PopNoteStack()
    {
        for(UInt8 pitch = 0; pitch < kNumMIDIPitches; ++pitch) {
            for(UInt8 ch = 0; ch < kNumMIDIChannels; ++ch) {
                auto &stack = GetNoteStack(pitch, ch);
                
                while(stack != 0) {
                    ProcessInfo::MidiMessage msg;
                    msg.offset_ = 0;
                    msg.channel_ = ch;
                    msg.ppq_pos_ = 0;
                    msg.data_ = MidiDataType::NoteOff { (UInt8)pitch, (UInt8)64 };
                    note_off_cache_.push_back(msg);
                    --stack;
                }
            }
        }
    }
    
    std::atomic<UInt32> & GetNoteStack(UInt32 pitch, UInt32 channel) {
        assert(pitch < kNumMIDIPitches);
        assert(channel < kNumMIDIChannels);
        return note_stack_[channel * kNumMIDIPitches + pitch];
    }
    
    std::atomic<UInt32> const & GetNoteStack(UInt32 pitch, UInt32 channel) const {
        assert(pitch < kNumMIDIPitches);
        assert(channel < kNumMIDIChannels);
        return note_stack_[channel * kNumMIDIPitches + pitch];
    }
    
    std::vector<ProcessInfo::MidiMessage> events_;
    std::array<std::atomic<UInt32>, kNumMIDIPitches * kNumMIDIChannels> note_stack_;
    std::vector<ProcessInfo::MidiMessage> note_off_cache_;
};

struct EventBufferList : ProcessInfo::IEventBufferList
{
    std::vector<EventBuffer> buffers_;
    
    UInt32 GetNumBuffers() const override {
        return buffers_.size();
    }
    
    void SetNumBuffers(UInt32 num)
    {
        buffers_.resize(num);
    }
    
    EventBuffer * GetBuffer(UInt32 index) override
    {
        assert(index < buffers_.size());
        return &buffers_[index];
    }
    
    EventBuffer const * GetBuffer(UInt32 index) const override
    {
        assert(index < buffers_.size());
        return &buffers_[index];
    }
    
    void Clear() {
        for(auto &b: buffers_) { b.Clear(); }
    }
    
    void Sort() {
        for(auto &b: buffers_) { b.Sort(); }
    }
    
    void ApplyCachedNoteOffs() {
        for(auto &b: buffers_) { b.ApplyCachedNoteOffs(); }
    }
    
    ArrayRef<ProcessInfo::MidiMessage const> GetRef(UInt32 channel_index) const
    {
        return GetBuffer(channel_index)->GetRef();
    }
};

NS_HWM_END
