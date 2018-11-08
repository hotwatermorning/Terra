#include "TestAudioProcessor.hpp"

NS_HWM_BEGIN

template<class Container>
void ClearPlayingNotes(Container &c)
{
    std::for_each(c.begin(), c.end(), [](auto &x) { x.store(false); });
}

TestAudioProcessor::TestAudioProcessor()
{
    ClearPlayingNotes(playing_sequence_notes_);
    ClearPlayingNotes(added_interactive_notes_);
    ClearPlayingNotes(playing_interactive_notes_);
}

TestAudioProcessor::~TestAudioProcessor()
{}

void TestAudioProcessor::SetInstrument(std::shared_ptr<Vst3Plugin> plugin)
{
    assert(plugin);
    
    auto lock = lf_.make_lock();
    plugin_ = plugin;
    plugin_->SetBlockSize(block_size_);
    plugin_->SetSamplingRate(sample_rate_);
    plugin_->Resume();
}

std::shared_ptr<Vst3Plugin> TestAudioProcessor::RemoveInstrument()
{
    auto bypass = MakeScopedBypassRequest(bypass_, true);
    
    auto lock = lf_.make_lock();
    auto plugin = std::move(plugin_);
    lock.unlock();

    bypass.reset();

    ClearPlayingNotes(playing_sequence_notes_);
    if(plugin) {
        plugin->Suspend();
    }
    
    return plugin;
}

std::shared_ptr<Vst3Plugin> TestAudioProcessor::GetInstrument() const
{
    auto lock = lf_.make_lock();
    return plugin_;
}

void TestAudioProcessor::SetSequence(std::shared_ptr<Sequence> seq)
{
    auto bypass = MakeScopedBypassRequest(bypass_, true);
    
    auto lock = lf_.make_lock();
    sequence_ = seq;
}

Transporter & TestAudioProcessor::GetTransporter()
{
    return tp_;
}

Transporter const & TestAudioProcessor::GetTransporter() const
{
    return tp_;
}

void TestAudioProcessor::StartProcessing(double sample_rate,
                                         SampleCount max_block_size,
                                         int num_input_channels,
                                         int num_output_channels)
{
    sample_rate_ = sample_rate;
    block_size_ = max_block_size;
    num_device_inputs_ = num_input_channels;
    num_device_outputs_ = num_output_channels;
}

std::vector<int> GetPlayingSequenceImpl(TestAudioProcessor::PlayingNoteList const &list)
{
    std::vector<int> ret;
    for(int i = 0; i < list.size(); ++i) {
        if(list[i].load()) { ret.push_back(i); }
    }
    
    return ret;
}

std::vector<int> TestAudioProcessor::GetPlayingSequenceNotes() const
{
    return GetPlayingSequenceImpl(playing_sequence_notes_);
}

std::vector<int> TestAudioProcessor::GetPlayingInteractiveNotes() const
{
    return GetPlayingSequenceImpl(playing_interactive_notes_);
}

void TestAudioProcessor::AddInteractiveNote(int note_number)
{
    added_interactive_notes_[note_number] = true;
}

void TestAudioProcessor::RemoveInteractiveNote(int note_number)
{
    added_interactive_notes_[note_number] = false;
}

template<class F>
class TraversalCallback
:   public Transporter::ITraversalCallback
{
public:
    TraversalCallback(F f) : f_(std::forward<F>(f)) {}
    
    void Process(TransportInfo const &info, SampleCount length) override
    {
        f_(info, length);
    }
    
    F f_;
};

template<class F>
TraversalCallback<F> MakeTraversalCallback(F f)
{
    return TraversalCallback<F>(std::forward<F>(f));
}

void TestAudioProcessor::Process(SampleCount block_size, float const * const * input, float **output)
{
    ScopedBypassGuard guard;
    
    for(int i = 0; i < 50; ++i) {
        guard = ScopedBypassGuard(bypass_);
        if(guard) { break; }
    }
    
    if(!guard) { return; }
    
    auto plugin = GetInstrument();
    if(!plugin) { return; }
    
    SampleCount num_processed = 0;
    
    auto cb = MakeTraversalCallback([&, this](TransportInfo const &ti, SampleCount length) {
        auto const frame_begin = ti.sample_pos_;
        auto const frame_end = ti.sample_pos_ + length;
        
        auto in_this_frame = [&](auto pos) { return frame_begin <= pos && pos < frame_end; };
        auto in_rewound_frame = [&](auto pos) { return frame_begin <= pos && pos < ti.last_end_pos_; };
        
        std::shared_ptr<Sequence> seq;
        {
            auto lock = lf_.make_lock();
            seq = sequence_;
        }
        
        bool const need_stop_all_sequence_notes
        = (ti.playing_ == false)
        || (ti.playing_ && ti.sample_pos_ < ti.last_end_pos_);

        if(need_stop_all_sequence_notes) {
            for(int i = 0; i < playing_sequence_notes_.size(); ++i) {
                if(playing_sequence_notes_[i].load()) {
                    plugin->AddNoteOff(i);
                    playing_sequence_notes_[i].store(false);
                }
            }
        }
        
        if(ti.playing_) {
            std::for_each(seq->notes_.begin(), seq->notes_.end(), [&](Sequence::Note const &note) {
                if(in_this_frame(note.pos_)) {
                    plugin->AddNoteOn(note.pitch_);
                    playing_sequence_notes_[note.pitch_] = true;
                }
                if(in_this_frame(note.GetEndPos())) {
                    plugin->AddNoteOff(note.pitch_);
                    playing_sequence_notes_[note.pitch_] = false;
                }
            });
        }
        
        for(int i = 0; i < added_interactive_notes_.size(); ++i) {
            if(added_interactive_notes_[i] && !playing_interactive_notes_[i]) {
                plugin->AddNoteOn(i);
                playing_interactive_notes_[i] = true;
            } else if(!added_interactive_notes_[i] && playing_interactive_notes_[i]) {
                plugin->AddNoteOff(i);
                playing_interactive_notes_[i] = false;
            }
        }
        
        auto const result = plugin->ProcessAudio(ti, length);
        auto num_channels_to_copy = std::min<int>(num_device_outputs_, plugin->GetNumOutputs());
        
        for(int ch = 0; ch < num_channels_to_copy; ++ch) {
            std::copy_n(result[ch], length, output[ch] + num_processed);
        }
        num_processed += length;
    });
    
    tp_.Traverse(block_size, &cb);
}

void TestAudioProcessor::StopProcessing()
{
    
}

NS_HWM_END
