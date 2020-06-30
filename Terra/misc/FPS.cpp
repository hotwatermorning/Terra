#include "FPS.hpp"

#include <numeric>

NS_HWM_BEGIN

FPS::FPS()
{
    history_.resize(kNumHistory);
    std::fill(history_.begin(), history_.end(), 1 / 60.0);
    last_ = clock_t::now();
}

void FPS::Update()
{
    auto const now = clock_t::now();
    auto dur = std::chrono::duration<double>(now - last_).count();
    dur = std::max(dur, 0.0001);
    history_[index_] = 1.0 / dur;
    index_ = (index_ + 1) % kNumHistory;
    last_ = now;
}

double FPS::GetFPS() const
{
    auto const sum = std::reduce(history_.begin(), history_.end());
    auto const avg = sum / kNumHistory;
    return avg;
}

NS_HWM_END
