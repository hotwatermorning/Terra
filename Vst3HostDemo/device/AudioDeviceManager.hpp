#pragma once

#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

enum class AudioDriverType {
    kUnknown,
    kDirectSound,
    kMME,
    kASIO,
    kWDMKS,
    kWASAPI,
    kCoreAudio,
    kALSA,
    kJACK,
};

enum class AudioDeviceIOType {
    kInput,
    kOutput,
};

std::string to_string(AudioDriverType type);
std::wstring to_wstring(AudioDriverType type);

std::string to_string(AudioDeviceIOType io);
std::wstring to_wstring(AudioDeviceIOType io);

struct AudioDeviceInfo
{
    AudioDriverType driver_ = AudioDriverType::kUnknown;
    AudioDeviceIOType io_type_ = AudioDeviceIOType::kOutput;
    String name_;
    int num_channels_ = 0;
};

class IAudioDeviceCallback
{
protected:
    IAudioDeviceCallback()
    {}
    
public:
    virtual ~IAudioDeviceCallback()
    {}
    
    virtual
    void StartProcessing(double sample_rate,
                         SampleCount max_block_size,
                         int num_input_channels,
                         int num_output_channels) = 0;
    
    virtual
    void Process(SampleCount block_size, float const * const * input, float **output) = 0;
    
    virtual
    void StopProcessing() = 0;
};

class AudioDeviceManager final
:   public SingleInstance<AudioDeviceManager>
{
public:
    AudioDeviceManager();
    ~AudioDeviceManager();
    
    std::vector<AudioDeviceInfo> Enumerate();

    //! @pre IsOpened() == false
    bool Open(AudioDeviceInfo const *input_device,
              AudioDeviceInfo const *output_device,
              double sample_rate,
              SampleCount block_size);
    
    //! オープンが成功しても、その場ですぐにコールバックに処理が渡されるわけではない。
    //! Start()を呼び出したタイミングで、ストリームの処理が開始し、コールバックに処理が渡るようになる。
    void Start();

    //! ストリームの処理を停止する。
    void Stop();
    
    //! 停止中かどうかを返す。
    bool IsStopped() const;

    //! デバイスを閉じる (Stop()を事前に呼び出しておく必要はない)
    void Close();
    bool IsOpened() const;
    
    void AddCallback(IAudioDeviceCallback *cb);
    //! 取り除いた場合はtrueが、見つからなくて何もしなかった場合はfalseが返る。
    bool RemoveCallback(IAudioDeviceCallback const *cb);
    void RemoveAllCallbacks();
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
