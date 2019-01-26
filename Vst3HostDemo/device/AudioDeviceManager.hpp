#pragma once

#include "../misc/SingleInstance.hpp"
#include "../misc/Either.hpp"
#include "./DeviceIOType.hpp"

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

std::string to_string(AudioDriverType type);
std::wstring to_wstring(AudioDriverType type);

struct AudioDeviceInfo
{
    AudioDriverType driver_ = AudioDriverType::kUnknown;
    DeviceIOType io_type_ = DeviceIOType::kOutput;
    String name_;
    int num_channels_ = 0;

    std::vector<double> supported_sample_rates_;
    
    bool IsSampleRateSupported(double rate) const {
        auto const pred = [rate](auto x) { return x == rate; };
        return std::any_of(supported_sample_rates_.begin(),
                           supported_sample_rates_.end(),
                           pred);
    }
};

class AudioDevice
{
protected:
    AudioDevice() {}
    
public:
    virtual ~AudioDevice() {}
    
    virtual
    AudioDeviceInfo const * GetDeviceInfo(DeviceIOType io) const = 0;
    
    virtual
    double GetSampleRate() const = 0;
    
    virtual
    SampleCount GetBlockSize() const = 0;
    
    //! デバイスのフレーム処理を開始する。
    /*! @note デバイスオープン後、明示的に Start() を呼び出すまでは、デバイスのフレーム処理は開始しない。
     */
    virtual
    void Start() = 0;
    
    //! 指定したオーディオデーバイスのフレーム処理を停止する。
    virtual
    void Stop() = 0;
    
    //! 指定したオーディオデバイスが停止中かどうかを返す。
    virtual
    bool IsStopped() const = 0;
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
    
    AudioDriverType GetDefaultDriver() const;
    
    //! デバイスを列挙する
    /*! デバイスがオープンした状態で呼び出してはいけない。
     */
    std::vector<AudioDeviceInfo> Enumerate();
    
    enum ErrorCode {
        kAlreadyOpened,
        kDeviceNotFound,
        kInvalidParameters,
        kUnknown,
    };
    struct Error {
        Error(ErrorCode code, String msg) : code_(code), error_msg_(msg) {}
        ErrorCode code_;
        String error_msg_;
    };
    using OpenResult = Either<Error, AudioDevice *>;
    
    OpenResult Open(AudioDeviceInfo const *input_device,
                    AudioDeviceInfo const *output_device,
                    double sample_rate,
                    SampleCount block_size);
    
    //! オープンしているデバイスを返す。
    /*! IsOpened() == falseのときはnullptrが返る。
     */
    AudioDevice * GetDevice() const;

    //! デバイスを閉じる
    /*! @note デバイスのStop()メンバ関数を事前に呼び出しておく必要はない
     */
    void Close();
    
    //! デバイスがオープンされているかどうかを返す。
    bool IsOpened() const;

    //! フレーム処理のコールバックを登録する。
    /*! @note この関数は、必ずデバイスが Close() された状態で呼び出すこと。
     */
    void AddCallback(IAudioDeviceCallback *cb);
    
    //! 登録してあるコールバックを取り除く
    //! 取り除いた場合はtrueが、見つからなくて何もしなかった場合はfalseが返る。
    /*! @note この関数は、必ずデバイスが Close() された状態で呼び出すこと。
     */
    bool RemoveCallback(IAudioDeviceCallback const *cb);
   
    //! すべてのコールバックを取り除く
    /*! @note この関数は、必ずデバイスが Close() された状態で呼び出すこと。
     */
    void RemoveAllCallbacks();
    
private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
