#pragma once

#include <cassert>
#include <iterator>
#include <utility>

NS_HWM_BEGIN

struct PeakMeter
{
	typedef double dB_t;
	typedef int Msec;

	size_t	sampling_rate_;
	Msec	hold_time_;
	dB_t	release_speed_;
	dB_t	peak_;
	dB_t	level_;
	size_t	holding_;
	size_t	hold_samples_;
	dB_t	highest_level_;
	dB_t	peak_release_level_per_sample_;
	dB_t	release_level_per_sample_;
	dB_t	minimum_dB_;

	static int const DEFAULT_MINIMUM_DB = -640;
	static int const DEFAULT_RELEASE_SPEED = -96 / 2;

	PeakMeter(size_t sampling_rate, dB_t release_speed = DEFAULT_RELEASE_SPEED, dB_t minimum_dB = DEFAULT_MINIMUM_DB)
		:	sampling_rate_(sampling_rate)
		,	minimum_dB_(minimum_dB)
		,	hold_time_(0)
		,	release_speed_(-minimum_dB)
		,	peak_(minimum_dB)
		,	level_(minimum_dB)
		,	release_level_per_sample_(-minimum_dB)
		,	peak_release_level_per_sample_(-minimum_dB)
		,	highest_level_(minimum_dB)
		,	holding_(0)
		,	hold_samples_(0)
	{
		SetReleaseSpeed(release_speed);
	}

	void swap(PeakMeter &rhs)
	{
		std::swap(minimum_dB_, rhs.minimum_dB_);
		std::swap(sampling_rate_, rhs.sampling_rate_);
		std::swap(hold_time_, rhs.hold_time_);
		std::swap(release_speed_, rhs.release_speed_);
		std::swap(peak_, rhs.peak_);
		std::swap(level_, rhs.level_);
		std::swap(release_level_per_sample_, rhs.release_level_per_sample_);
		std::swap(peak_release_level_per_sample_, rhs.peak_release_level_per_sample_);
		std::swap(highest_level_, rhs.highest_level_);
		std::swap(holding_, rhs.holding_);
		std::swap(hold_samples_, rhs.hold_samples_);
	}

	size_t	GetSamplingRate	() const { return sampling_rate_; }

	Msec	GetHoldTime		() const { return hold_time_; }

	//! ReleaseSpeedは、一秒あたりピークレベルがどれだけ変化するかをdBで表す。
	//! -96.0であれば、一秒に-96dBまでメータが下降する
	//! 0.0であればメーターは張り付いたまま下降しない
	dB_t	GetReleaseSpeed	() const { return -release_speed_; }

	//! 現在のピーク値を取得する。
	//! ホールドタイムの設定により、
	//! より大きなピーク値が設定されない場合はピーク値が維持される。
	dB_t	GetPeak			() const { return peak_; }

	//! 現在の音量レベル値を取得する。
	//! リリースタイムの設定により、
	//! 無音状態になっても即座に0になるわけではない。
	dB_t	GetLevel		() const { return level_; }

	//! リセット状態からの最大値を取得する。
	//! この値は明示的にResetHighestLevelを呼ばない限り0.0へ初期化されない。
	dB_t	GetHighestLevel	() const { return highest_level_; }

	dB_t	GetMinimumLevel	() const { return minimum_dB_; }

public:
	void	SetHoldTime		(Msec duration)
	{
		hold_time_ = duration;
		hold_samples_ = static_cast<size_t>(GetSamplingRate() * (duration / 1000.0));
	}

	void	SetReleaseSpeed	(dB_t speed)
	{
		release_speed_ = -speed;
		release_level_per_sample_ = release_speed_ / GetSamplingRate();
	}

	void	SetSamplingRate	(size_t sampling_rate)
	{
		dB_t const release_speed = GetReleaseSpeed();
		Msec const hold_time = GetHoldTime();

		sampling_rate_ = sampling_rate;
		
		SetReleaseSpeed(release_speed);
		SetHoldTime(hold_time);
	}

	void	ResetHighestLevel()
	{
		highest_level_ = GetMinimumLevel();
	}

	//! Converterは、イテレータの指す値を[0.0 .. 0.1]にマッピングするために使用する、double(std::iterator_traits<InputIterator>::reference)というシグネチャをもつ
	//! 関数や、関数オブジェクトである。
	//! 呼び出す側がIteratorAdaptorを使用すればよいのだが、実際それは面倒なので、ここでConverterを受けるようにしている。
	template<class InputIterator, class Converter>
	void	SetSamples	(InputIterator begin, InputIterator end, Converter convert)
	{
		double tmp_peak = peak_;
		double tmp_highest_level = highest_level_;
		double tmp_level = level_;
		double tmp_peak_release_level_per_sample = peak_release_level_per_sample_;
		size_t tmp_holding = holding_;
		double const tmp_release_level_per_sample = release_level_per_sample_;
		size_t const tmp_hold_samples = hold_samples_;
		size_t const tmp_sampling_rate = sampling_rate_;

		for(InputIterator it = begin; it != end; ++it) {
			if(tmp_holding == 0 && tmp_peak > minimum_dB_) {
				tmp_peak -= tmp_peak_release_level_per_sample;
				if(tmp_peak_release_level_per_sample < tmp_release_level_per_sample) {
					double const tmp = tmp_peak_release_level_per_sample * (1 + (1.0 / tmp_sampling_rate) * 4);
					tmp_peak_release_level_per_sample = (std::min)(tmp, tmp_release_level_per_sample);
				}
			}

			tmp_level -= tmp_release_level_per_sample;
			if(tmp_level < minimum_dB_) { tmp_level = minimum_dB_; }

			double const val = convert(*it);
			bool new_level = false;
			if(tmp_level < val) {
				tmp_level = val;
				new_level = true;
			}

			if(tmp_peak < tmp_level) {
				tmp_peak = tmp_level;
				if(tmp_highest_level < tmp_peak) {
					tmp_highest_level = tmp_peak;
				}

				if(new_level) {
					tmp_holding = tmp_hold_samples;
					tmp_peak_release_level_per_sample = tmp_release_level_per_sample / 4;
				}
			}

			if(tmp_holding > 0) {
				--tmp_holding;
			}
		}

		peak_							= tmp_peak;
		highest_level_					= tmp_highest_level;
		level_							= tmp_level;
		peak_release_level_per_sample_	= tmp_peak_release_level_per_sample;
		holding_						= tmp_holding;
	}

	template<class InputIterator>
	void	SetSamples	(InputIterator begin, InputIterator end)
	{
		SetSamples(begin, end, [](typename std::iterator_traits<InputIterator>::value_type x) -> double { return x; });
	}

	struct zero_iterator
		:	std::iterator<std::input_iterator_tag, double>
	{
		typedef zero_iterator this_type;
		zero_iterator()
			:	begin_(0)
			,	end_(0)
			,	minimum_dB_(0)
		{}

		zero_iterator(dB_t minimum_dB, size_t size)
			:	begin_(0)
			,	end_(size)
			,	minimum_dB_(minimum_dB)
		{}

		double		operator*() const { return minimum_dB_; }

		this_type &	operator++() {
			assert(begin_ < end_);
			begin_++;
			return *this;
		}

		this_type operator++(int) {
			this_type tmp(*this);
			++(*this);
			return tmp;
		}

	private:
		int begin_;
		int end_;
		dB_t minimum_dB_;

		friend bool operator==(this_type const &lhs, this_type const &rhs)
		{
			return
				(lhs.begin_ == rhs.begin_ && lhs.end_ == rhs.end_) ||
				(lhs.begin_ == lhs.end_ && rhs.begin_ == rhs.end_);
		}

		friend bool operator!=(this_type const &lhs, this_type const &rhs)
		{
			return !(lhs == rhs);
		}
	};

	void	Consume(size_t n)
	{
		SetSamples(zero_iterator(minimum_dB_, n), zero_iterator());
	}
};

inline
void swap(PeakMeter &lhs, PeakMeter &rhs)
{
	lhs.swap(rhs);
}

NS_HWM_END
