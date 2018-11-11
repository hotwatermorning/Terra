#pragma once

#include <array>
#include <memory>

#include <functional>

#include "../../transport/TransportInfo.hpp"
#include "../../misc/ListenerService.hpp"
#include "../../misc/Buffer.hpp"
#include "../../misc/ArrayRef.hpp"

NS_HWM_BEGIN

struct Vst3Note
{
    enum class Type {
        kNoteOn,
        kNoteOff
    };
    
    Vst3Note() = default;
    Vst3Note(SampleCount offset, double ppq_pos, int channel, int pitch, int velocity, Type type)
    {
        SetOffset(offset);
        SetPPQPos(ppq_pos);
        SetChannel(channel);
        SetPitch(pitch);
        SetVelocity(velocity);
        SetNoteType(type);
    }
    
    SampleCount GetOffset() const { return offset_; }
    
    void SetOffset(SampleCount offset) {
        assert(offset_ >= 0);
        offset_ = offset;
    }
    
    double GetPPQPos() const { return ppq_pos_; }
    void SetPPQPos(double ppq_pos) { ppq_pos_ = ppq_pos; }
    
    int GetChannel() const { return channel_; }
    void SetChannel(int channel) {
        assert(0 <= channel && channel <= 15);
        channel_ = channel;
    }
    
    int GetPitch() const { return pitch_; }
    void SetPitch(int pitch) {
        assert(0 <= pitch && pitch <= 127);
        pitch_ = pitch;
    }
    
    int GetVelocity() const { return velocity_; }
    void SetVelocity(int velocity) {
        assert(0 <= velocity && velocity <= 127);
        velocity_ = velocity;
    }
    
    Type GetNoteType() const { return type_; }
    void SetNoteType(Type type) { type_ = type; }
    
    bool IsNoteOn() const { return GetNoteType() == Type::kNoteOn; }
    bool IsNoteOff() const { return GetNoteType() == Type::kNoteOff; }

private:
    SampleCount offset_ = 0;
    double ppq_pos_ = 0;
    int channel_ = 0;
    int pitch_ = 0;
    int velocity_ = 0;
    Type type_ = Type::kNoteOff;
};
    
using String = std::wstring;

//! VST3のプラグインを表すクラス
/*!
	Vst3PluginFactoryから作成可能
	Vst3PluginFactoryによってロードされたモジュール（.vst3ファイル）がアンロードされてしまうため、
	作成したVst3Pluginが破棄されるまで、Vst3PluginFactoryを破棄してはならない。
*/
class Vst3Plugin
{
public:
	class Impl;
    class HostContext;

	class ParameterAccessor
	{
    public:
		ParameterAccessor(Vst3Plugin *owner);

		typedef Steinberg::Vst::ParamValue value_t;

		size_t	size() const;

		value_t get_by_index(size_t index) const;
		void	set_by_index(size_t index, value_t new_value);
		
		value_t get_by_id	(Steinberg::Vst::ParamID id) const;
		void	set_by_id	(Steinberg::Vst::ParamID id, value_t value);

		Steinberg::Vst::ParameterInfo
				info(size_t index) const;

	private:
		Vst3Plugin *owner_;
	};
	friend ParameterAccessor;

public:
	Vst3Plugin(std::unique_ptr<Impl> pimpl,
               std::unique_ptr<HostContext> host_context,
               std::function<void(Vst3Plugin const *p)> on_destruction);
    
	virtual ~Vst3Plugin();

	ParameterAccessor &			GetParams();
	ParameterAccessor const &	GetParams() const;

	String GetEffectName() const;
	size_t	GetNumInputs() const;
    size_t  GetNumOutputs() const;
	void	Resume();
	void	Suspend();
	bool	IsResumed() const;
	void	SetBlockSize(int block_size);
	void	SetSamplingRate(int sampling_rate);
    
    class EditorCloseListener
    {
    public:
        virtual
        ~EditorCloseListener() {}
        
        virtual
        void OnEditorClosed(Vst3Plugin *plugin) = 0;
    };
    ListenerService<EditorCloseListener> ec_listeners_;

    void AddEditorCloseListener(EditorCloseListener *li);
    void RemoveEditorCloseListener(EditorCloseListener *li);
    
	bool	HasEditor		() const;

#if defined(_MSC_VER)
    using WindowHandle = HWND;
#else
    using WindowHandle = NSView *;
#endif
    
	bool	OpenEditor		(WindowHandle wnd, Steinberg::IPlugFrame *frame = nullptr);
    
	void	CloseEditor		();
	bool	IsEditorOpened	() const;
	Steinberg::ViewRect
			GetPreferredRect() const;

	size_t	GetProgramCount() const;
	String  GetProgramName(size_t index) const;

	size_t	GetProgramIndex() const;

	void	SetProgramIndex(size_t index);

	//! パラメータの変更を次回の再生フレームでAudioProcessorに送信して適用するために、
	//! 変更する情報をキューに貯める
	void	EnqueueParameterChange(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

	void	RestartComponent(Steinberg::int32 flag);
    
    template<class T>
    struct ProcessBufferInfo
    {
        BufferRef<T> buffer_;
        SampleCount sample_offset_ = 0;
    };
    
    struct ProcessInfo
    {
        TransportInfo const *           ti_ = nullptr;
        SampleCount                     frame_length_;
        ArrayRef<Vst3Note>              notes_;
        ProcessBufferInfo<float const>  input_;
        ProcessBufferInfo<float>        output_;
    };

	void Process(ProcessInfo &pi);
    
private:
	std::unique_ptr<ParameterAccessor>	parameters_;
	std::unique_ptr<Impl> pimpl_;
    std::unique_ptr<HostContext> host_context_;
    std::function<void(Vst3Plugin const *p)> on_destruction_;
};

//class PlugFrame
//:   Steinberg::Vst::IPlugFrame
//{
//    OBJ_METHODS(PlugFrame, Steinberg::Vst::IPlugFrame)
//    REFCOUNT_METHODS(Steinberg::Vst::IPlugFrame)
//
//public:
//    DEFINE_INTERFACES
//    DEF_INTERFACE(Steinberg::Vst::IPlugFrame)
//    DEF_INTERFACE(Vst::IComponentHandler)
//    DEF_INTERFACE(Vst::IComponentHandler2)
//    END_DEFINE_INTERFACES(FObject)
//};

NS_HWM_END
