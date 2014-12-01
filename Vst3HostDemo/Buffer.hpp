#pragma once

namespace hwm {

template<class T>
struct Buffer
{
	typedef T value_type;
	Buffer()
		:	channel_(0)
		,	sample_(0)
	{}

	Buffer(size_t num_channels, size_t num_samples)
	{
		resize(num_channels, num_samples);
	}

	size_t samples() const { return sample_; }
	size_t channels() const { return channel_; }

	value_type ** data() { return buffer_heads_.data(); }
	value_type const * const * data() const { return buffer_heads_.data(); }

	void resize(size_t num_channels, size_t num_samples)
	{
		std::vector<value_type> tmp(num_channels * num_samples);
		std::vector<value_type *> tmp_heads(num_channels);

		channel_ = num_channels;
		sample_ = num_samples;

		buffer_.swap(tmp);
		buffer_heads_.swap(tmp_heads);
		for(size_t i = 0; i < num_channels; ++i) {
			buffer_heads_[i] = buffer_.data() + (i * num_samples);
		}
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

	size_t channel_;
	size_t sample_;
};

}	// hwm