#pragma once

#include <algorithm>
#include <boost/function.hpp>

#include <balor/gui/all.hpp>
#include <balor/graphics/all.hpp>

#include "./namespace.hpp"

namespace hwm {

struct PeakLevel
{
	PeakLevel()
		:	peak_(0)
		,	level_(0)
	{}

	PeakLevel(double peak, double level)
		:	peak_(peak)
		,	level_(level)
	{}

	double peak_;
	double level_;
};

class PeakMeterDisplay
{
public:
	typedef boost::function<void(PeakLevel *pl, size_t num_channels)> peak_level_query_handler_t;

private:
	gui::Panel		wnd_;
	gpx::Bitmap		bitmap_;
	gpx::Graphics	gpx_;
	gui::Timer		updater_;
	gpx::Font		font_small_;
	std::vector<PeakLevel> peak_levels_;

	peak_level_query_handler_t	peak_level_query_handler_;

public:
	PeakMeterDisplay(gui::Control &parent, int x, int y, int width, int height)
		:	wnd_(parent, x, y, width, height)
		,	bitmap_(width, height)
		,	gpx_(bitmap_)
		,	updater_(wnd_, 10)
		,	font_small_(L"ÉÅÉCÉäÉI", 12, gpx::Font::Style::regular, gpx::Font::Quality::default)
	{
		wnd_.onPaint() = [this] (gui::Panel::Paint &e) { OnPaintPeak(e); };
		updater_.onRun() = [this] (gui::Timer::Run &) { wnd_.invalidate(); };
		updater_.start();
	}

public:
	void SetQueryLevelHandler(peak_level_query_handler_t handler)
	{
		peak_level_query_handler_ = handler;
	}

	void SetNumChannels(size_t num_channels)
	{
		peak_levels_.resize(num_channels);
	}

private:
	void OnPaintPeak(gui::Panel::Paint &e)
	{
		peak_level_query_handler_(peak_levels_.data(), peak_levels_.size());

		typedef double dB_t;
		static auto const scale = [](dB_t x) { return x / 96.0 + 1.0; };

		double const max_height = 1.3; // ç≈í·ÉåÉxÉãÇ©ÇÁ0dBÇ‹Ç≈dBç∑ÇÃ1.3î{Ç‹Ç≈ÇÃÉfÅ[É^Çï\é¶Ç∑ÇÈ
		int const peak_bar_height = 2;
	
		gpx::Color base			(20, 30, 60);
		gpx::Color level		(50, 240, 50);
		gpx::Color peak			(230, 230, 50);
		gpx::Color level_clip	(240, 60, 60);
		gpx::Color peak_clip	(255, 40, 70);

		auto make_rect = [](int x1, int y1, int x2, int y2) {
			return balor::Rect(x1, y1, x2 - x1, y2 - y1);
		};

		// îwåiï`âÊ
		gpx_.brush(base);
		gpx_.clear();

		int const channel_width = (std::max<int>)(wnd_.size().width / peak_levels_.size(), 1);

		for(size_t i = 0; i < peak_levels_.size(); ++i) {
			auto const &peak_level = peak_levels_[i];
			double const peak_height = scale(peak_level.peak_);
			double const level_height = scale(peak_level.level_);

			int left = i * wnd_.size().width / peak_levels_.size();
			int right = left + channel_width;

			// levelï`âÊ
			if(peak_level.level_ < 1.0) {
				gpx_.brush(level);
				gpx_.pen(level);
			} else {
				gpx_.brush(level_clip);
				gpx_.pen(level_clip);
			}
			gpx_.drawRectangle(
				make_rect(
					left, 
					static_cast<int>((1.0 - (level_height / max_height)) * wnd_.size().height),
					right,
					wnd_.size().height ) );

			// peakï`âÊ
			if(peak_level.peak_ < 1.0) {
				gpx_.brush(peak);
				gpx_.pen(peak);
			} else {
				gpx_.brush(peak_clip);
				gpx_.pen(peak_clip);
			}

			balor::Rect rc_peak(
				balor::Point(
					left,
					static_cast<int>((1.0 - (peak_height / max_height)) * wnd_.size().height )
					),
				balor::Size(
					channel_width,
					peak_bar_height)
					);
			gpx_.drawRectangle(rc_peak);
		}

		// 0.0dBà íu
		gpx::Pen clip_border(balor::graphics::Color(255, 255, 255), 1, gpx::Pen::Style::dot);
		gpx_.pen(clip_border);
		gpx_.drawLine(
			wnd_.size().width / 4,
			(1.0 - (1.0 / max_height)) * wnd_.size().height,
			wnd_.size().width * 3 / 4,
			(1.0 - (1.0 / max_height)) * wnd_.size().height );

		e.graphics().copy(0, 0, wnd_.size().width, wnd_.size().height, gpx_);
	}
};

}	// ::hwm