#pragma once

#include <chrono>
#include <vector>

NS_HWM_BEGIN

struct FPS
{
    using clock_t = std::chrono::steady_clock;

    FPS();

    void Update();

    double GetFPS() const;

private:
    static constexpr int kNumHistory = 3;
    int index_ = 0;
    std::vector<double> history_;
    clock_t::time_point last_;
};

NS_HWM_END
