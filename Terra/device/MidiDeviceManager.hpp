#pragma once

#include "../misc/SingleInstance.hpp"
#include "../data_type/MidiDataType.hpp"
#include "./DeviceIOType.hpp"
#include "./MidiDevice.hpp"

NS_HWM_BEGIN

//! デバイスとやり取りするMIDI
struct DeviceMidiMessage
{
    using second_t = double;
    
    MidiDevice *device_ = nullptr;
    //! ある時刻から見たタイムスタンプ
    //! MIDI入力の場合は、std::chrono::steady_clockのtime_since_epoch()からの時刻。
    //! MIDI出力の場合は、MidiDeviceManager::SendMessagesに渡したepochからの時刻。
    second_t time_stamp_ = 0;
    UInt8 channel_ = 0;
    
    template<class To>
    To * As() { return mpark::get_if<To>(&data_); }
    
    template<class To>
    To const * As() const { return mpark::get_if<To>(&data_); }
    
    static
    DeviceMidiMessage Create(MidiDevice *device,
                             second_t time_delta,
                             UInt8 status,
                             UInt8 data1,
                             UInt8 data2 = 0);
    
    //! 書き込み先のバッファとランニングステータスを渡して、バッファにMIDIメッセージのバイト列を書き込み。
    //! バッファは必ずサイズが0にリセットされてから書き込みが行われる
    //! 不正なデータがあった場合は、bufが空にされ、falseが返る。
    bool ToBytes(std::vector<UInt8> &buf) const;
    
    //! 書き込み先のバッファとランニングステータスを渡して、バッファにMIDIメッセージのバイト列を書き込み。
    //! ランニングステータスも最新のステータスバイトで上書きする。
    //! バッファは必ずサイズが0にリセットされてから書き込みが行われる
    //! 不正なデータがあった場合は、bufが空にされ、falseが返る。(running_statusは呼び出し時の状態のまま上書きされないで残る）
    bool ToBytes(std::vector<UInt8> &buf, UInt8 &running_status) const;
    
    using DataType = MidiDataType::VariantType;
    DataType data_;
};

//! オーディオデバイス側をマスタークロックにして駆動するため、
//! このクラスには、このクラスを利用する側に向けたコールバックの仕組みは設けない
class MidiDeviceManager
:   public SingleInstance<MidiDeviceManager>
{
public:
    MidiDeviceManager();
    ~MidiDeviceManager();

    std::vector<MidiDeviceInfo> Enumerate();
    
    //! デバイスをオープンする。失敗時はエラーメッセージがStringとして返る。
    MidiDevice * Open(MidiDeviceInfo const &info, String *error = nullptr);
    bool IsOpened(MidiDeviceInfo const &info) const;
    void Close(MidiDevice const *device);
    
    //! return the device if opened.
    MidiDevice * GetDevice(MidiDeviceInfo const &info);
    
    //! この瞬間までに取得できたMIDIメッセージを返す。
    //! システムメッセージには未対応。
    //! 現在のタイムスタンプを返す。
    double GetMessages(std::vector<DeviceMidiMessage> &ms);
    
    //! MIDIメッセージを送信する。
    //! システムメッセージには未対応。
    //! 各DeviceMidiMessageのtime_stampは、epochからの時間として扱う
    void SendMessages(std::vector<DeviceMidiMessage> const &ms, double epoch = 0);
    
private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

NS_HWM_END
