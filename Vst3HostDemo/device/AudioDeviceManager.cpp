#include <mutex>
#include <portaudio.h>

#include "./AudioDeviceManager.hpp"
#include "../misc/Buffer.hpp"
#include "../misc/StrCnv.hpp"

NS_HWM_BEGIN

std::string to_string(AudioDriverType type)
{
    using A = AudioDriverType;
    switch (type) {
        case A::kDirectSound: return "DirectSound";
        case A::kMME: return "MME";
        case A::kASIO: return "ASIO";
        case A::kWDMKS: return "WDM KS";
        case A::kWASAPI: return "WASAPI";
        case A::kCoreAudio: return "CoreAudio";
        case A::kALSA: return "ALSA";
        case A::kJACK: return "JACK";
        default: return "Unknown";
    }
}

std::wstring to_wstring(AudioDriverType type)
{
    return to_wstr(to_string(type));
}

AudioDriverType ToAudioDriverType(PaHostApiIndex index)
{
    using A = AudioDriverType;
    switch (index) {
        case paDirectSound: return A::kDirectSound;
        case paMME: return A::kMME;
        case paASIO: return A::kASIO;
        case paWDMKS: return A::kWDMKS;
        case paWASAPI: return A::kWASAPI;
        case paCoreAudio: return A::kCoreAudio;
        case paALSA: return A::kALSA;
        case paJACK: return A::kJACK;
        default: return A::kUnknown;
    }
}

PaHostApiIndex FromAudioDriverType(AudioDriverType type)
{
    using A = AudioDriverType;
    switch (type) {
        case A::kDirectSound: return paDirectSound;
        case A::kMME: return paMME;
        case A::kASIO: return paASIO;
        case A::kWDMKS: return paWDMKS;
        case A::kWASAPI: return paWASAPI;
        case A::kCoreAudio: return paCoreAudio;
        case A::kALSA: return paALSA;
        case A::kJACK: return paJACK;
        default: return paInDevelopment;
    }
}

class AudioDeviceImpl
:   public AudioDevice
{
public:
    AudioDeviceImpl(AudioDeviceInfo const *input,
                    AudioDeviceInfo const *output,
                    double sample_rate,
                    SampleCount block_size,
                    std::vector<IAudioDeviceCallback *> &callbacks,
                    PaStream *stream)
    :   sample_rate_(sample_rate)
    ,   block_size_(block_size)
    ,   callbacks_(callbacks)
    ,   stream_(stream)
    {
        if(input) { input_ = *input; }
        if(output) { output_ = *output; }
        
        assert(input_ || output_);
        assert(!input_ || input_->io_type_ == DeviceIOType::kInput);
        assert(!output_ || output_->io_type_ == DeviceIOType::kOutput);
        assert(sample_rate_ > 0);
        assert(block_size_ > 0);
        assert(stream_);
        
        num_inputs_ = (input_ ? input_->num_channels_ : 0);
        num_outputs_ = (output_ ? output_->num_channels_ : 0);
        tmp_input_float_.resize(num_inputs_, block_size);
        tmp_output_float_.resize(num_outputs_, block_size);
    }
    
    AudioDeviceImpl(AudioDeviceImpl const &rhs) = delete;
    AudioDeviceImpl & operator=(AudioDeviceImpl const &rhs) = delete;
    AudioDeviceImpl(AudioDeviceImpl &&rhs) = delete;
    AudioDeviceImpl & operator=(AudioDeviceImpl &&rhs) = delete;
    
    AudioDeviceInfo const * GetInfo(DeviceIOType io) const override
    {
        auto const &info = (io == DeviceIOType::kInput) ? input_ : output_;
        return info ? &*info : nullptr;
    }
    
    double GetSampleRate() const override { return sample_rate_; }
    SampleCount GetBlockSize() const override { return block_size_; }
    
    void Start() override
    {
        if(Pa_IsStreamStopped(stream_)) {
            ForEachCallbacks([this](auto *cb) {
                cb->StartProcessing(sample_rate_, block_size_, num_inputs_, num_outputs_);
            });
            Pa_StartStream(stream_);
        }
    }
    
    void Stop() override
    {
        if(!Pa_IsStreamStopped(stream_)) {
            Pa_StopStream(stream_);
            ForEachCallbacks([](auto *cb) { cb->StopProcessing(); });
        }
    }
    
    bool IsStopped() const override
    {
        return Pa_IsStreamStopped(stream_);
    }
    
    PaStream * GetStream() { return stream_; }
    
    PaStreamCallbackResult StreamCallback(const void *input, void *output,
                                          unsigned long block_size, const PaStreamCallbackTimeInfo *timeInfo,
                                          PaStreamCallbackFlags statusFlags)
    {
        ClearBuffer<float>(output, block_size);
        InvokeCallbacks<float>(input, output, block_size);
        return paContinue;
    }
    
private:
    std::optional<AudioDeviceInfo> input_;
    std::optional<AudioDeviceInfo> output_;
    double sample_rate_ = 0;
    SampleCount block_size_ = 0;
    
    std::vector<IAudioDeviceCallback *> &callbacks_;
    PaStream *stream_ = nullptr;
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
};

class AudioDeviceManager::Impl
{
public:
    Impl()
    {}
    
    std::vector<IAudioDeviceCallback *> callbacks_;
    std::unique_ptr<AudioDeviceImpl> device_;
    
    static
    int StaticStreamCallback(const void *input, void *output,
                             unsigned long frameCount, const PaStreamCallbackTimeInfo *timeInfo,
                             PaStreamCallbackFlags statusFlags, void *userData)
    {
        auto *self = reinterpret_cast<Impl *>(userData);
        assert(self);
        
        auto *device = self->device_.get();
        assert(device);
        
        return device->StreamCallback(input, output, frameCount, timeInfo, statusFlags);
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

AudioDriverType AudioDeviceManager::GetDefaultDriver() const
{
    auto index = Pa_GetDefaultHostApi();
    if(index < 0) {
        ShowErrorMsg(index);
        return AudioDriverType::kUnknown;
    }
    
    auto *info = Pa_GetHostApiInfo(index);
    return ToAudioDriverType(info->type);
}

std::vector<AudioDeviceInfo> AudioDeviceManager::Enumerate()
{
    auto const device_count = Pa_GetDeviceCount();
    
    if(device_count < 0) {
        ShowErrorMsg(device_count);
        return {};
    }
    
    std::vector<AudioDeviceInfo> result;
    for(PaDeviceIndex i = 0; i < device_count; ++i) {
        auto *info = Pa_GetDeviceInfo(i);
        auto *host_api_info = Pa_GetHostApiInfo(info->hostApi);
        
        if(info->maxInputChannels >= 0) {
            AudioDeviceInfo tmp {
                ToAudioDriverType(host_api_info->type),
                DeviceIOType::kInput,
                to_wstr(info->name),
                info->maxInputChannels
            };
            result.push_back(tmp);
        }
        
        if(info->maxOutputChannels >= 0) {
            AudioDeviceInfo tmp {
                ToAudioDriverType(host_api_info->type),
                DeviceIOType::kOutput,
                to_wstr(info->name),
                info->maxOutputChannels
            };
            result.push_back(tmp);
        }
    }
    
    return result;
}

AudioDeviceManager::OpenResult
AudioDeviceManager::Open(AudioDeviceInfo const *input_device,
                         AudioDeviceInfo const *output_device,
                         double sample_rate,
                         SampleCount block_size)
{
    if(IsOpened()) {
        return Error(ErrorCode::kAlreadyOpened, L"Device already opened.");
    }
    
    if(input_device == nullptr && output_device == nullptr) {
        return Error(ErrorCode::kInvalidParameters, L"Invalid parameters.");
    }
    
    PaStreamParameters ip = {};
    PaStreamParameters op = {};
    PaStreamParameters *pip = nullptr;
    PaStreamParameters *pop = nullptr;
    PaStreamFlags flags = 0;
    
    auto find_index = [](auto const &target) -> int {
        int num = Pa_GetDeviceCount();
        for(int i = 0; i < num; ++i) {
            auto const *info = Pa_GetDeviceInfo(i);
            auto const *host_api_info = Pa_GetHostApiInfo(info->hostApi);
            if(target.name_ == to_wstr(info->name) &&
               target.driver_ == ToAudioDriverType(host_api_info->type))
            {
                return i;
            }
        }
        return -1;
    };
    
    if(input_device) {
        ip.channelCount = input_device->num_channels_;
        ip.device = find_index(*input_device);
        ip.sampleFormat = paFloat32;
        if(ip.device >= 0) { pip = &ip; }
    }
    
    if(output_device) {
        op.channelCount = output_device->num_channels_;
        op.device = find_index(*output_device);
        op.sampleFormat = paFloat32;
        if(op.device >= 0) { pop = &op; }
    }
    
    hwm::wdout << L"Open Device ({}, {})"_format(pip ? input_device->name_ : L"N/A",
                                                 pop ? output_device->name_ : L"N/A")
    << std::endl;
    
    if(!pip && !pop) {
        return Error(ErrorCode::kDeviceNotFound, L"Device not found");
    }
    
    PaStream *stream;
    PaError err = Pa_OpenStream(&stream, pip, pop,
                                sample_rate, block_size, flags,
                                &Impl::StaticStreamCallback, pimpl_.get());
    ShowErrorMsg(err);
    if(err != paNoError) {
        // todo portaudioのエラーコードに合わせて整理
        return Error(ErrorCode::kUnknown, L"Unknown Error");
    }
    
    pimpl_->device_ = std::make_unique<AudioDeviceImpl>(input_device, output_device,
                                                        sample_rate, block_size,
                                                        pimpl_->callbacks_,
                                                        stream);
    
    return pimpl_->device_.get();
}

AudioDevice * AudioDeviceManager::GetDevice() const
{
    return pimpl_->device_.get();
}

void AudioDeviceManager::Close()
{
    if(!IsOpened()) { return; }
    
    pimpl_->device_->Stop();
    
    PaError err = Pa_CloseStream(pimpl_->device_->GetStream());
    ShowErrorMsg(err);
    
    pimpl_->device_.reset();
}

bool AudioDeviceManager::IsOpened() const
{
    return !!pimpl_->device_;
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
    assert(!IsOpened());
    
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
    assert(!IsOpened());
    
    pimpl_->callbacks_.clear();
}

NS_HWM_END
