#include "Stopwatch.hpp"

#include <cassert>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <chrono>

#if defined(_MSC_VER)
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/times.h>
#endif

NS_HWM_BEGIN

namespace {

thread_local int g_stopwatch_instance_count_;
std::mutex g_stopwatch_callback_mutex_;

#if !defined(_MSC_VER)
Int64 tick_factor() // multiplier to convert ticks　to nanoseconds; -1 if unknown
{
  static Int64 tick_factor = 0;
  if (!tick_factor)
  {
      if ((tick_factor = ::sysconf(_SC_CLK_TCK)) <= 0) {
          tick_factor = -1;
      } else {
          tick_factor = INT64_C(1000000000) / tick_factor;  // compute factor
          if (!tick_factor) {
              tick_factor = -1;
          }
      }
  }
  return tick_factor;
}
#endif

void get_current_time(Stopwatch::TimeInfo &t)
{
    using C = std::chrono::high_resolution_clock;
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(C::now().time_since_epoch());

    t.wall_ = now.count();

#if defined(_MSC_VER)
    FILETIME creation, exit;
    if (::GetProcessTimes(::GetCurrentProcess(), &creation, &exit,
            (LPFILETIME)&t.system_, (LPFILETIME)&t.user_))
    {
        t.user_   *= 100;  // Windows uses 100 nanosecond ticks
        t.system_ *= 100;
    } else {
        t.user_ = t.system_ = Stopwatch::Nanosec(-1);
    }
#else
    tms tm;
    auto c = ::times(&tm);
    if (c == static_cast<decltype(c)>(-1)) {
        t.system_ = t.user_ = Stopwatch::Nanosec(-1);
    } else {
        t.system_ = Stopwatch::Nanosec(tm.tms_stime + tm.tms_cstime);
        t.user_ = Stopwatch::Nanosec(tm.tms_utime + tm.tms_cutime);
        Int64 const factor = tick_factor();
        if(factor != -1) {
            t.user_ *= factor;
            t.system_ *= factor;
        } else {
            t.user_ = t.system_ = Stopwatch::Nanosec(-1);
        }
    }
#endif
}

} // namespace

Stopwatch::TimeInfo & Stopwatch::TimeInfo::operator+=(TimeInfo const &rhs) noexcept
{
    user_   += rhs.user_;
    system_ += rhs.system_;
    wall_   += rhs.wall_;

    return *this;
}

Stopwatch::TimeInfo Stopwatch::TimeInfo::operator+(TimeInfo const &rhs) const noexcept
{
    auto tmp = *this;
    return tmp += rhs;
}

Stopwatch::TimeInfo & Stopwatch::TimeInfo::operator-=(TimeInfo const &rhs) noexcept
{
    user_   -= rhs.user_;
    system_ -= rhs.system_;
    wall_   -= rhs.wall_;

    return *this;
}

Stopwatch::TimeInfo Stopwatch::TimeInfo::operator-(TimeInfo const &rhs) const noexcept
{
    auto tmp = *this;
    return tmp -= rhs;
}

Stopwatch::Stopwatch()
{
    is_valid_ = false;
}

Stopwatch::Stopwatch(std::string_view label, callback_type cb)
{
    ++g_stopwatch_instance_count_;
    members_.label_ = label;
    members_.cb_ = cb;
    get_current_time(members_.begin_);
    
    is_valid_ = true;
}

Stopwatch::Stopwatch(Stopwatch &&rhs)
{
    members_ = std::move(rhs.members_);
    is_valid_ = rhs.is_valid_;
    rhs.Invalidate();
}

Stopwatch & Stopwatch::operator=(Stopwatch &&rhs)
{
    if(this == &rhs) { return *this; }

    Invalidate();

    members_ = std::move(rhs.members_);
    is_valid_ = rhs.is_valid_;
    rhs.Invalidate();

    return *this;
}

Stopwatch::~Stopwatch()
{
    Invalidate();
}

void Stopwatch::DefaultCallbackFunction(std::string_view label, int depth, TimeInfo const &t)
{
    std::stringstream ss;

    for(int i = 0; i < depth; ++i) {
        ss << "  ";
    }

#if defined(_MSC_VER)
    // do nothing.
#else
    ss << "↱";
#endif

    constexpr double kNanosecPrecision = 1000 * 1000 * 1000.0;

    ss
    << std::setprecision(6) << std::fixed
    << "[" << std::left << std::setw(30) << label << "]: "
    << std::right
    << "{ "
    << std::setw(9) << (t.user_ / kNanosecPrecision)
    << ", "
    << std::setw(9) << (t.system_ / kNanosecPrecision)
    << ", "
    << std::setw(9) << (t.wall_ / kNanosecPrecision)
    << " }"
    << "(user, system, wall)";

    std::cout << ss.str() << std::endl;
}

Stopwatch Stopwatch::Create(std::string_view label, callback_type cb)
{
    return Stopwatch(label, std::move(cb));
}

Stopwatch Stopwatch::Null()
{
    return Stopwatch();
}

bool Stopwatch::IsNull() const noexcept
{
    return is_valid_ == false;
}

void Stopwatch::Output() noexcept
{
    try {
        TimeInfo cur;
        get_current_time(cur);

        std::lock_guard lock(g_stopwatch_callback_mutex_);
        members_.cb_(members_.label_, g_stopwatch_instance_count_ - 1, cur - members_.begin_);
    } catch(std::exception &e) {
        assert("should never fail" && false);
        // no rethrow.
    }
}

void Stopwatch::Invalidate()
{
    if(is_valid_) {
        Output();
        is_valid_ = false;

        g_stopwatch_instance_count_ -= 1;
        assert(g_stopwatch_instance_count_ >= 0);
    }
}

NS_HWM_END
