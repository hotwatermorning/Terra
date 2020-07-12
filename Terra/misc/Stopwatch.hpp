#pragma once

#include <cstdint>
#include <string>
#include "Preprocessor.hpp"

#if !defined(ENABLE_TERRA_STOPWATCH)
#define ENABLE_TERRA_STOPWATCH 1
#endif

NS_HWM_BEGIN

class [[nodiscard]] Stopwatch
{
public:
    using Nanosec = std::int64_t;

    struct TimeInfo
    {
        Nanosec user_;
        Nanosec system_;
        Nanosec wall_;

        TimeInfo & operator+=(TimeInfo const &rhs) noexcept;
        TimeInfo operator+(TimeInfo const &rhs) const noexcept;
        TimeInfo & operator-=(TimeInfo const &rhs) noexcept;
        TimeInfo operator-(TimeInfo const &rhs) const noexcept;
    };

    using callback_type = std::function<void(std::string_view label,
                                             int depth,
                                             TimeInfo const &t)>;

private:
    Stopwatch();
    Stopwatch(std::string_view label, callback_type cb);

    Stopwatch(Stopwatch const &) = delete;
    Stopwatch & operator=(Stopwatch const &) = delete;

public:
    Stopwatch(Stopwatch &&);
    Stopwatch & operator=(Stopwatch &&);
    ~Stopwatch();

public:
    static
    void DefaultCallbackFunction(std::string_view label, int depth, TimeInfo const &t);

    static
    Stopwatch Create(std::string_view label, callback_type cb = &Stopwatch::DefaultCallbackFunction);

    static
    Stopwatch Null();

    //! Call the callback function passed to the constructor with the current elapsed time.
    void Output() noexcept;

    bool IsNull() const noexcept;

private:
    struct MemberSet {
        std::string label_;
        callback_type cb_;
        TimeInfo begin_;
    };

    MemberSet members_;

    void Invalidate();
    bool is_valid_;
};

NS_HWM_END

#if ENABLE_TERRA_STOPWATCH

//! 指定したラベルを持つ Stopwatch を作成する。
#define TERRA_STOPWATCH(label) TERRA_STOPWATCH_I(label)
#define TERRA_STOPWATCH_I(label) \
    auto TERRA_PP_CAT(terra_stopwatch_unnamed_, __LINE__) = Stopwatch::Create(label);

//! 指定したラベルと名前を持つ Stopwatch を作成する。
//! この Stopwatch は、 TERRA_STOPWATCH_FINISH マクロを使うことで、スコープが終わるより先に計測を終了できる。
#define TERRA_STOPWATCH_WITH_NAME(label, name) TERRA_STOPWATCH_WITH_NAME_I(label, name)
#define TERRA_STOPWATCH_WITH_NAME_I(label, name) \
    auto TERRA_PP_CAT(terra_stopwatch_, name) = Stopwatch::Create(label);

//! 指定した名前を持つ Stopwatch の計測を終了する。
#define TERRA_STOPWATCH_FINISH(name) \
    do { \
        assert(TERRA_PP_CAT(terra_stopwatch_, name).IsNull() == false); \
        TERRA_PP_CAT(terra_stopwatch_, name) = Stopwatch::Null(); \
    } while(0);

#else

#define TERRA_STOPWATCH(label)
#define TERRA_STOPWATCH_FINISH(label)

#endif
