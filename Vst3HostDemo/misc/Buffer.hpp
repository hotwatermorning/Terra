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

	Buffer(size_t num_channels, size_t num_samples)
	{
		resize(num_channels, num_samples);
	}

	size_t samples() const { return samples_; }
	size_t channels() const { return channels_; }

	value_type ** data() { return buffer_heads_.data(); }
	value_type const * const * data() const { return buffer_heads_.data(); }

	void resize(size_t num_channels, size_t num_samples)
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

	void resize_samples(size_t num_samples)
	{
		resize(channels(), num_samples);
	}

	void resize_channels(size_t num_channels)
	{
		resize(num_channels, samples());
	}

public:
	std::vector<value_type> buffer_;
	std::vector<value_type *> buffer_heads_;

	size_t channels_;
	size_t samples_;
};

template<class T>
class BufferRef
{
public:
    BufferRef()
    {
        static T * dummy_ = nullptr;
        data_ = &dummy_;
        channels_ = 0;
        samples_ = 0;
    }
    
    template<class U>
    struct get_data_type {
        using type = typename std::conditional_t<std::is_const<U>::value, U const * const *, U**>;
    };
    
    using data_type = typename get_data_type<T>::type;
    using const_data_type = typename get_data_type<std::add_const_t<T>>::type;
    
    template<class U>
    BufferRef(Buffer<U> &buffer) : BufferRef(buffer.data(), buffer.channels(), buffer.samples())
    {}
    
    BufferRef(data_type data, int num_channels, int num_samples)
    {
        data_ = data;
        channels_ = num_channels;
        samples_ = num_samples;
    }
    
    size_t samples() const { return samples_; }
    size_t channels() const { return channels_; }
    
    data_type data() { return data_; }
    const_data_type data() const { return data_; }
    
    void fill(T value = T())
    {
        for(int ch = 0; ch < channels_; ++ch) {
            auto ch_data = data()[ch];
            std::fill_n(ch_data, ch_data + samples_, value);
        }
    }
    
private:
    data_type data_;
    size_t channels_;
    size_t samples_;
};

NS_HWM_END
