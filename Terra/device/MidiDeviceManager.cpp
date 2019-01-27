#include "MidiDeviceManager.hpp"
#include "RtMidi.h"
#include "../misc/StrCnv.hpp"
#include "../misc/ArrayRef.hpp"
#include "../misc/LockFactory.hpp"
#include "../misc/ThreadSafeRingBuffer.hpp"

NS_HWM_BEGIN

using namespace MidiDataType;

namespace {
    using clock_t = std::chrono::steady_clock;

    double get_timestamp()
    {
        auto const dur = clock_t::now().time_since_epoch();
        return std::chrono::duration<double>(dur).count();
    }
}

DeviceMidiMessage DeviceMidiMessage::Create(MidiDevice *device,
                                            second_t time_stamp,
                                            UInt8 status,
                                            UInt8 data1,
                                            UInt8 data2)
{
    DeviceMidiMessage m;
    m.device_ = device;
    m.time_stamp_ = time_stamp;
    m.channel_ = (status & 0xF);
    
    switch(status & 0xF0) {
        case MessageType::kNoteOff:
            m.data_ = NoteOff { data1, data2 };
            break;
        case MessageType::kNoteOn: {
            if(data2 > 0) {
                m.data_ = NoteOn { data1, data2 };
            } else {
                m.data_ = NoteOff { data1, 64 };
            }
            break;
        }
        case MessageType::kPolyphonicKeyPressure:
            m.data_ = PolyphonicKeyPressure { data1, data2 };
            break;
        case MessageType::kControlChange:
            m.data_ = ControlChange { data1, data2 };
            break;
        case MessageType::kProgramChange:
            m.data_ = ProgramChange { data1 };
            break;
        case MessageType::kChannelPressure:
            m.data_ = ChannelPressure { data1 };
            break;
        case MessageType::kPitchBendChange:
            m.data_ = PitchBendChange { data1, data2 };
            break;
        default:
            assert(false);
    }
    
    return m;
}

bool DeviceMidiMessage::ToBytes(std::vector<UInt8> &buf) const
{
    UInt8 running_status = 0;
    return ToBytes(buf, running_status);
}

//! 書き込み先のバッファとランニングステータスを渡して、バッファにMIDIメッセージのバイト列を書き込み。
//! ランニングステータスも最新のステータスバイトで上書きする。
bool DeviceMidiMessage::ToBytes(std::vector<UInt8> &buf, UInt8 &running_status) const
{
    buf.clear();

    auto write_status = [&](auto new_status) {
        if(new_status == running_status) { return; }
        running_status = new_status;
        buf.push_back(new_status);
    };
    
    auto write1 = [&](auto data1) { buf.push_back(data1); };
    auto write2 = [&](auto data1, auto data2) { buf.push_back(data1); buf.push_back(data2); };
    
    // ステータスバイトの準備
    if(auto p = As<NoteOff>()) {
        write_status(MessageType::kNoteOff | channel_);
        write2(p->pitch_, p->off_velocity_);
    } else if(auto p = As<NoteOn>()) {
        if(p->velocity_ > 0) {
            write_status(MessageType::kNoteOn | channel_);
            write2(p->pitch_, p->velocity_);
        } else {
            write_status(MessageType::kNoteOff | channel_);
            write2(p->pitch_, 64);
        }
    } else if(auto p = As<PolyphonicKeyPressure>()) {
        write_status(MessageType::kPolyphonicKeyPressure | channel_);
        write2(p->pitch_, p->value_);
    } else if(auto p = As<ControlChange>()) {
        write_status(MessageType::kControlChange | channel_);
        write2(p->control_number_, p->data_);
    } else if(auto p = As<ProgramChange>()) {
        write_status(MessageType::kProgramChange | channel_);
        write1(p->program_number_);
    } else if(auto p = As<ChannelPressure>()) {
        write_status(MessageType::kChannelPressure | channel_);
        write1(p->value_);
    } else if(auto p = As<PitchBendChange>()) {
        write_status(MessageType::kPitchBendChange | channel_);
        write2(p->value_lsb_, p->value_msb_);
    } else {
        return false;
    }
    
    return true;
}

struct MidiIn
:   public MidiDevice
{
    //! @throw RtMidiError
    MidiIn(MidiDeviceInfo const &info, std::function<void(DeviceMidiMessage const &)> on_input)
    :   info_(info)
    ,   on_input_(on_input)
    {
        assert(info.io_type_ == DeviceIOType::kInput);
        midi_in_.ignoreTypes();
        midi_in_.setCallback(Callback, this);
        midi_in_.setErrorCallback(ErrorCallback, this);
        
        int n = -1;
        for(int i = 0; i < midi_in_.getPortCount(); ++i) {
            if(to_wstr(midi_in_.getPortName(i)) == info_.name_id_) {
                n = i;
                break;
            }
        }
        if(n == -1) { throw std::runtime_error("unknown device"); }
        midi_in_.openPort(n, to_utf8(info_.name_id_));
    }
    
    ~MidiIn()
    {
        try {
            Close();
        } catch(std::exception &e) {
            assert(false);
        }
    }
    
    void Close() { midi_in_.closePort(); }
    
    MidiDeviceInfo const & GetDeviceInfo() const override { return info_; }
    
    
private:
    MidiDeviceInfo info_;
    RtMidiIn midi_in_;
    std::optional<UInt8> running_status_;
    std::function<void(DeviceMidiMessage const &)> on_input_;
    
    static
    void Callback(double, std::vector<unsigned char> *message, void *userData)
    {
        assert(message);
        static_cast<MidiIn *>(userData)->OnCallback(*message);
    }
    
    static
    void ErrorCallback(RtMidiError::Type type, const std::string &errorText, void *userData)
    {
        static_cast<MidiIn *>(userData)->OnErrorCallback(type, errorText);
    }

    void OnCallback(std::vector<unsigned char> const &message)
    {
        if(message.size() == 0) {
            return;
        }

        auto const now = get_timestamp();
        bool const has_status_byte = ((message[0] & 0xF0) != 0);
        
        //! running statusがないのにステータスバイトがない => 不正なメッセージなので無視
        if(!has_status_byte && !running_status_) {
            return;
        }
        
        if(has_status_byte) { running_status_ = message[0]; }
        UInt8 const status_byte = *running_status_;
        
        auto get_bytes = [&](int length) -> std::optional<ArrayRef<const unsigned char>> {
            if(message.size() < (length + has_status_byte)) { return std::nullopt; }
            return ArrayRef<const unsigned char> {
                message.data() + has_status_byte,
                message.data() + length + has_status_byte
            };
        };
        
        DeviceMidiMessage m;
        
        switch(status_byte & 0xF0) {
            case MessageType::kNoteOff:
            case MessageType::kNoteOn:
            case MessageType::kPolyphonicKeyPressure:
            case MessageType::kControlChange:
            case MessageType::kPitchBendChange:
            {
                if(auto bytes = get_bytes(2)) {
                    m = DeviceMidiMessage::Create(this, now, status_byte, (*bytes)[0], (*bytes)[1]);
                }
                break;
            }
                
            case MessageType::kProgramChange:
            case MessageType::kChannelPressure:
            {
                if(auto bytes = get_bytes(1)) {
                    m = DeviceMidiMessage::Create(this, now, status_byte, (*bytes)[0]);
                }
                break;
            }
                
            default:
                assert(false);
        }
        
        on_input_(m);
    }
    
    void OnErrorCallback(RtMidiError::Type type, const std::string &errorText)
    {
        // todo: error handling
    }
};

struct MidiOut
:   public MidiDevice
{
    //! @throw RtMidiError
    MidiOut(MidiDeviceInfo const &info)
    :   info_(info)
    {
        messages_.reserve(2048);
        
        midi_out_.setErrorCallback(ErrorCallback, this);
        
        int n = -1;
        for(int i = 0; i < midi_out_.getPortCount(); ++i) {
            if(to_wstr(midi_out_.getPortName(i)) == info_.name_id_) {
                n = i;
                break;
            }
        }
        if(n == -1) { throw std::runtime_error("unknown device"); }
        midi_out_.openPort(0, to_utf8(info_.name_id_));
    }
    
    ~MidiOut()
    {
        try {
            Close();
        } catch(std::exception &e) {
            assert(false);
        }
    }
    
    void Close() { midi_out_.closePort(); }
    
    MidiDeviceInfo const & GetDeviceInfo() const override { return info_; }
    
    void SendMessages(std::vector<DeviceMidiMessage> const &ms)
    {
        std::vector<UInt8> buf;
        buf.reserve(3);
        for(auto const &m : ms) {
            bool const successful = m.ToBytes(buf, running_status_);
            if(!successful) { continue; }
            midi_out_.sendMessage(&buf);
        }
    }
    
private:
    MidiDeviceInfo info_;
    RtMidiOut midi_out_;
    
    std::vector<DeviceMidiMessage> messages_;
    UInt8 running_status_ = 0;

    static
    void ErrorCallback(RtMidiError::Type type, const std::string &errorText, void *userData)
    {
        static_cast<MidiOut *>(userData)->OnErrorCallback(type, errorText);
    }
    
    void OnErrorCallback(RtMidiError::Type type, const std::string &errorText)
    {
        // todo: error handling
    }
};

struct MidiDeviceManager::Impl
{
    static constexpr int kNumCapacity = 4096;
    Impl()
    :   input_messages_(kNumCapacity)
    {}
    
    using MidiInPtr = std::shared_ptr<MidiIn>;
    using MidiOutPtr = std::shared_ptr<MidiOut>;

    std::vector<MidiInPtr> ins_;
    std::vector<MidiOutPtr> outs_;
    SingleChannelThreadSafeRingBuffer<DeviceMidiMessage> input_messages_;
    
    void AddMidiMessage(DeviceMidiMessage const &m)
    {
        std::string str_bytes;
        std::vector<UInt8> buf(3);
        m.ToBytes(buf);
        for(auto b: buf) {
            str_bytes += " {:02x}"_format((unsigned int)b);
        }
        hwm::dout << "[{:03.6f}]:{}"_format(m.time_stamp_, str_bytes) << std::endl;
        
        for( ; ; ) {
            auto result = input_messages_.Push(&m, 1);
            if(result.error_code() == ThreadSafeRingBufferErrorCode::kTokenUnavailable) { continue; }
            else { break; }
        }
    }
    
    LockFactory lf_in_;
    LockFactory lf_out_;
};

MidiDeviceManager::MidiDeviceManager()
:   pimpl_(std::make_unique<Impl>())
{}

MidiDeviceManager::~MidiDeviceManager()
{}

std::vector<MidiDeviceInfo> MidiDeviceManager::Enumerate()
{
    std::vector<MidiDeviceInfo> list;
    
    RtMidiIn in;
    auto const num_ins = in.getPortCount();
    for(int i = 0; i < num_ins; ++i) {
        list.push_back({DeviceIOType::kInput, to_wstr(in.getPortName(i))});
    }
    
    RtMidiOut out;
    auto const num_outs = out.getPortCount();
    for(int i = 0; i < num_outs; ++i) {
        list.push_back({DeviceIOType::kOutput, to_wstr(out.getPortName(i))});
    }
    
    return list;
}

MidiDevice * MidiDeviceManager::Open(MidiDeviceInfo const &info, String *error)
{
    try {
        if(info.io_type_ == DeviceIOType::kInput) {
            auto p = std::make_shared<MidiIn>(info, [this](auto const &m) { pimpl_->AddMidiMessage(m); });
            {
                auto lock = pimpl_->lf_in_.make_lock();
                pimpl_->ins_.push_back(p);
            }
            return p.get();
        } else {
            auto p = std::make_shared<MidiOut>(info);
            {
                auto lock = pimpl_->lf_out_.make_lock();
                pimpl_->outs_.push_back(p);
            }
            return p.get();
        }
    } catch(std::exception &e) {
        if(error) { *error = to_wstr(e.what()); }
    }
    
    assert("never reach here" && false);
    return nullptr;
}

auto find_device = [](auto &container, auto name_id) {
    auto predicate = [name_id](auto const &device) { return device->GetDeviceInfo().name_id_ == name_id; };
    return std::find_if(std::begin(container), std::end(container), predicate);
};

bool MidiDeviceManager::IsOpened(MidiDeviceInfo const &info) const
{
    if(info.io_type_ == DeviceIOType::kInput) {
        auto lock = pimpl_->lf_in_.make_lock();
        return find_device(pimpl_->ins_, info.name_id_) != pimpl_->ins_.end();
    } else {
        auto lock = pimpl_->lf_out_.make_lock();
        return find_device(pimpl_->outs_, info.name_id_) != pimpl_->outs_.end();
    }
}

void MidiDeviceManager::Close(MidiDevice const *device)
{
    if(auto midi_in = dynamic_cast<MidiIn const *>(device)) {
        auto lock = pimpl_->lf_in_.make_lock();
        auto found = find_device(pimpl_->ins_, midi_in->GetDeviceInfo().name_id_);
        if(found == pimpl_->ins_.end()) { return; }
        
        auto moved = std::move(*found);
        pimpl_->ins_.erase(found);
        lock.unlock();
        
        moved.reset(); // close the device here
        
    } else if(auto midi_out = dynamic_cast<MidiOut const *>(device)) {
        auto lock = pimpl_->lf_out_.make_lock();
        auto found = find_device(pimpl_->outs_, midi_out->GetDeviceInfo().name_id_);
        if(found == pimpl_->outs_.end()) { return; }
        
        auto moved = std::move(*found);
        pimpl_->outs_.erase(found);
        lock.unlock();
        
        moved.reset(); // close the device here
        
    } else {
        assert("Unknown device type" && false);
    }
}

//! この瞬間までに取得できたMIDIメッセージを返す。
//! システムメッセージには未対応。
double MidiDeviceManager::GetMessages(std::vector<DeviceMidiMessage> &msg)
{
    msg.clear();
    
    for( ; ; ) {
        auto num = pimpl_->input_messages_.GetNumPoppable();
        msg.resize(num);
        
        auto result = pimpl_->input_messages_.PopOverwrite(msg.data(), num);
        if(result) { break; }
    }
    return get_timestamp();
}

//! MIDIメッセージを送信する。
//! システムメッセージには未対応。
void MidiDeviceManager::SendMessages(std::vector<DeviceMidiMessage> const &msg, double epoch)
{
    
}

NS_HWM_END
