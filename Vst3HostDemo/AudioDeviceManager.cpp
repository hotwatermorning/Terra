#include "./AudioDeviceManager.hpp"
#include <mutex>
#include "./Buffer.hpp"

NS_HWM_BEGIN

class AudioDeviceManager::Impl
{
public:
    Impl()
    {}
    
    std::vector<IAudioDeviceCallback *> callbacks_;
    
    PaStream *stream_ = nullptr;
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    int num_inputs_ = 0;
    int num_outputs_ = 0;
    Buffer<float> tmp_input_float_, tmp_output_float_;
    
    //! @tparam F is a functor where its signature is `void(IAudioDeviceCallback *)`
    template<class F>
    void ForEachCallbacks(F f) {
        std::for_each(callbacks_.begin(), callbacks_.end(), f);
    }
    
    template<class SampleType>
    void ClearBuffer(void *output, SampleCount block_size)
    {
        auto *p = reinterpret_cast<SampleType *>(output);
        std::fill_n(p, num_outputs_ * block_size, 0);
    }
    
    template<class SampleType>
    void InvokeCallbacks(const void *input, void *output, SampleCount block_size)
    {
        SampleType const * const * input_non_interleaved = nullptr;
        SampleType ** output_non_interleaved = nullptr;
        
        auto *pi = reinterpret_cast<SampleType const *>(input);
        for(int ch = 0; ch < num_inputs_; ++ch) {
            for(SampleCount smp = 0; smp < block_size; ++smp) {
                tmp_input_float_.data()[ch][smp] = pi[smp * num_inputs_ + ch];
            }
        }
        
        tmp_output_float_.fill(0.0);
        
        input_non_interleaved = tmp_input_float_.data();
        output_non_interleaved = tmp_output_float_.data();
        
        ForEachCallbacks([&](IAudioDeviceCallback *cb) {
            cb->Process(block_size, input_non_interleaved, output_non_interleaved);
        });
        
        auto *po = reinterpret_cast<SampleType *>(output);
        for(int ch = 0; ch < num_outputs_; ++ch) {
            for(SampleCount smp = 0; smp < block_size; ++smp) {
                po[smp * num_outputs_ + ch] = tmp_output_float_.data()[ch][smp];
            }
        }
    }
    
    PaStreamCallbackResult StreamCallback(const void *input, void *output,
                                          unsigned long block_size, const PaStreamCallbackTimeInfo *timeInfo,
                                          PaStreamCallbackFlags statusFlags)
    {
        ClearBuffer<float>(output, block_size);
        InvokeCallbacks<float>(input, output, block_size);
        return paContinue;
    }
    
    static
    int StaticStreamCallback(const void *input, void *output,
                                                unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo,
                                                PaStreamCallbackFlags statusFlags, void *userData)
    {
        auto *self = reinterpret_cast<Impl *>(userData);
        return self->StreamCallback(input, output, frameCount, timeInfo, statusFlags);
    }
};

template<class ErrCode>
auto ShowErrorMsg(ErrCode err)
{
    if(err != paNoError) {
        hwm::dout << Pa_GetErrorText(err) << std::endl;
    }
}

AudioDeviceManager::AudioDeviceManager()
:   pimpl_(std::make_unique<Impl>())
{
    PaError err = Pa_Initialize();
    ShowErrorMsg(err);
}

AudioDeviceManager::~AudioDeviceManager()
{
    PaError err = Pa_Terminate();
    ShowErrorMsg(err);
}

std::vector<PaDeviceInfo> AudioDeviceManager::Enumerate()
{
    auto const device_count = Pa_GetDeviceCount();

    if(device_count < 0) {
        ShowErrorMsg(device_count);
        return {};
    }

    std::vector<PaDeviceInfo> result;
    for(PaDeviceIndex i = 0; i < device_count; ++i) {
        auto *info = Pa_GetDeviceInfo(i);
        result.push_back(*info);
    }
    
    return result;
}

bool AudioDeviceManager::Open(PaDeviceInfo const *input_device,
                              PaDeviceInfo const *output_device,
                              double sample_rate,
                              SampleCount block_size)
{
    assert(!IsOpened());
    
    if(input_device == nullptr && output_device == nullptr) { return false; }
    
    PaStreamParameters ip = {};
    PaStreamParameters op = {};
    PaStreamParameters *pip = nullptr;
    PaStreamParameters *pop = nullptr;
    PaStreamFlags flags = 0;
    
    auto list = Enumerate();
    auto find_index = [&list](auto const &info) -> int {
        auto found = std::find_if(list.begin(), list.end(), [&info](auto const &x) {
            return
            std::string(info.name) == x.name &&
            info.hostApi == x.hostApi;
        });
        if(found == list.end()) { return -1; }
        else { return found - list.begin(); }
    };
    
    if(input_device) {
        ip.channelCount = input_device->maxInputChannels;
        ip.device = find_index(*input_device);
        ip.sampleFormat = paFloat32;
        if(ip.device >= 0) { pip = &ip; }
    }
    
    if(output_device) {
        op.channelCount = output_device->maxOutputChannels;
        op.device = find_index(*output_device);
        op.sampleFormat = paFloat32;
        if(op.device >= 0) { pop = &op; }
    }

    pimpl_->sample_rate_ = sample_rate;
    pimpl_->block_size_ = block_size;
    pimpl_->num_inputs_ = (pip ? pip->channelCount : 0);
    pimpl_->num_outputs_ = (pop ? pop->channelCount : 0);
    pimpl_->tmp_input_float_.resize(pimpl_->num_inputs_, block_size);
    pimpl_->tmp_output_float_.resize(pimpl_->num_outputs_, block_size);
    
    PaError err = Pa_OpenStream(&pimpl_->stream_, pip, pop,
                                sample_rate, block_size, flags,
                                &Impl::StaticStreamCallback, pimpl_.get());
    ShowErrorMsg(err);
    if(err != paNoError) {
        return false;
    }
    
    return true;
}

void AudioDeviceManager::Start()
{
    assert(IsOpened());
    
    if(Pa_IsStreamStopped(pimpl_->stream_)) {
        
        pimpl_->ForEachCallbacks([this](auto *cb) {
            cb->StartProcessing(pimpl_->sample_rate_,
                                pimpl_->block_size_,
                                pimpl_->num_inputs_,
                                pimpl_->num_outputs_);
        });
        Pa_StartStream(pimpl_->stream_);
    }
}

void AudioDeviceManager::Stop()
{
    assert(IsOpened());
    
    if(!Pa_IsStreamStopped(pimpl_->stream_)) {
        Pa_StopStream(pimpl_->stream_);
        pimpl_->ForEachCallbacks([this](auto *cb) {
            cb->StopProcessing();
        });
    }
}

bool AudioDeviceManager::IsStopped() const
{
    assert(IsOpened());
    return Pa_IsStreamStopped(pimpl_->stream_);
}

void AudioDeviceManager::Close()
{
    if(!IsOpened()) { return; }
    
    Stop();
    
    PaError err = Pa_CloseStream(pimpl_->stream_);
    ShowErrorMsg(err);
}

bool AudioDeviceManager::IsOpened() const
{
    return pimpl_->stream_;
}

void AudioDeviceManager::AddCallback(IAudioDeviceCallback *cb)
{
    assert(!IsOpened());
    
    auto found = std::find(pimpl_->callbacks_.begin(), pimpl_->callbacks_.end(), cb);
    assert(found == pimpl_->callbacks_.end());
    
    pimpl_->callbacks_.push_back(cb);
}

bool AudioDeviceManager::RemoveCallback(IAudioDeviceCallback const *cb)
{
    auto found = std::find(pimpl_->callbacks_.begin(),
                           pimpl_->callbacks_.end(),
                           cb);
    
    if(found != pimpl_->callbacks_.end()) {
        pimpl_->callbacks_.erase(found);
        return true;
    } else {
        return false;
    }
}

void AudioDeviceManager::RemoveAllCallbacks()
{
    pimpl_->callbacks_.clear();
}

NS_HWM_END
