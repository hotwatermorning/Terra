#pragma once

#include <vector>

NS_HWM_BEGIN

template<class T>
class Buffer
{
public:
	typedef T value_type;
	Buffer()
		:	channels_(0)
		,	samples_(0)
	{}

	Buffer(UInt32 num_channels, UInt32 num_samples)
	{
		resize(num_channels, num_samples);
	}

	UInt32 samples() const { return samples_; }
	UInt32 channels() const { return channels_; }

	value_type ** data() { return buffer_heads_.data(); }
	value_type const * const * data() const { return buffer_heads_.data(); }

	void resize(UInt32 num_channels, UInt32 num_samples)
	{
		std::vector<value_type> tmp(num_channels * num_samples);
		std::vector<value_type *> tmp_heads(num_channels);

		channels_ = num_channels;
		samples_ = num_samples;

		buffer_.swap(tmp);
		buffer_heads_.swap(tmp_heads);
		for(size_t i = 0; i < num_channels; ++i) {
			buffer_heads_[i] = buffer_.data() + (i * num_samples);
		}
	}
    
    void fill(T value = T())
    {
        std::fill(buffer_.begin(), buffer_.end(), value);
    }

	void resize_samples(UInt32 num_samples)
	{
		resize(channels(), num_samples);
	}

	void resize_channels(UInt32 num_channels)
	{
		resize(num_channels, samples());
	}

public:
	std::vector<value_type> buffer_;
	std::vector<value_type *> buffer_heads_;

	UInt32 channels_;
	UInt32 samples_;
};

template<class T>
class BufferRef
{
public:
    BufferRef()
    {
        static T * dummy_ = nullptr;
        data_ = &dummy_;
        num_channels_ = 0;
        num_samples_ = 0;
    }
    
    template<class U>
    struct get_data_type {
        using type = typename std::conditional_t<std::is_const<U>::value, U const * const *, U**>;
    };
    
    using data_type = typename get_data_type<T>::type;
    using const_data_type = typename get_data_type<std::add_const_t<T>>::type;
    
    template<class U>
    BufferRef(Buffer<U> &buffer)
    :   BufferRef(buffer.data(), buffer.channels(), buffer.samples())
    {}
    
    template<class U>
    BufferRef(Buffer<U> &buffer, UInt32 channel_from, UInt32 num_channels, UInt32 sample_from, UInt32 num_samples)
    {
        assert(channel_from + num_channels <= buffer.channels());
        assert(sample_from + num_samples <= buffer.samples());
        
        data_ = buffer.data();
        channel_from_ = channel_from;
        num_channels_ = num_channels;
        sample_from_ = sample_from;
        num_samples_ = num_samples;
    }
    
    BufferRef(data_type data, UInt32 num_channels, UInt32 num_samples)
    :   BufferRef(data, 0, num_channels, 0, num_samples)
    {}
    
    BufferRef(data_type data, UInt32 channel_from, UInt32 num_channels, UInt32 sample_from, UInt32 num_samples)
    {
        data_ = data;
        channel_from_ = channel_from;
        num_channels_ = num_channels;
        sample_from_ = sample_from;
        num_samples_ = num_samples;
    }
    
    UInt32 samples() const { return num_samples_; }
    UInt32 channels() const { return num_channels_; }
    UInt32 sample_from() const { return sample_from_; }
    UInt32 channel_from() const { return channel_from_; }
    
    data_type data() { return data_; }
    const_data_type data() const { return data_; }
    
    T * get_channel_data(UInt32 channel_index) {
        assert(channel_index < num_channels_);
        return data()[channel_index + channel_from_] + sample_from_;
    }
    
    std::add_const<T> * get_channel_data(UInt32 channel_index) const {
        assert(channel_index < num_channels_);
        return data()[channel_index + channel_from_] + sample_from_;
    }
    
    void fill(T value = T())
    {
        for(UInt32 ch = 0; ch < num_channels_; ++ch) {
            auto ch_data = data_[ch + channel_from_];
            std::fill_n(ch_data + sample_from_, num_samples_, value);
        }
    }
    
private:
    data_type data_;
    UInt32 channel_from_;
    UInt32 num_channels_;
    UInt32 sample_from_;
    UInt32 num_samples_;
};

NS_HWM_END
