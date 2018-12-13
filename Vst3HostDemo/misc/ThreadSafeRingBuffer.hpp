#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

#if defined(_DEBUG)
#include <iostream>
#include <iomanip>
#endif

NS_HWM_BEGIN

enum class ThreadSafeRingBufferErrorCode
{
    kSuccessful,            //!< 非エラーを表す
    kTokenUnavailable,      //!< トークンが使用中で取得できなかったことを表す
    kBufferInsufficient     //!< 内部バッファ中で利用可能なバッファのサイズが要求したサイズより小さい
};

struct ThreadSafeRingBufferResult
{
    using ErrorCode = ThreadSafeRingBufferErrorCode;
    
    ThreadSafeRingBufferResult(ErrorCode code = ErrorCode::kSuccessful) : code_(code) {}
    
    //! エラー状態のときにfalseを返す。
    explicit operator bool() const { return code_ == ErrorCode::kSuccessful; }
    ErrorCode error_code() const { return code_; }
    
private:
    ErrorCode code_;
};

//! データの追加と取り出しをそれぞれ別スレッドから安全に呼び出せるリングバッファ
template<class T>
class ThreadSafeRingBufferImpl
{
public:
    using ErrorCode = ThreadSafeRingBufferErrorCode;
    using Result = ThreadSafeRingBufferResult;
    using value_type = T;
    
    ThreadSafeRingBufferImpl(UInt32 num_channels, UInt32 capacity)
    {
        assert(num_channels > 0);
        assert(capacity > 0);
        
        // ここでサイズを一つ増やしているのは、
        // capacity満杯までデータを追加したときに、
        // read_pos_とwrite_pos_の位置が重なってデータが空なのか満杯なのかが判断できなくなってしまうのを
        // 回避するため。
        bufsize_ = capacity + 1;
        
        data_.resize(num_channels);
        for(auto &x: data_) { x.resize(bufsize_); }
        
        num_channels_ = num_channels;
        read_pos_ = 0;
        write_pos_ = 0;
        
        push_token_ = false;
        pop_token_ = false;
    }
    
    //! 全体の容量を返す
    UInt32 GetCapacity() const
    {
        return bufsize_ - 1;
    }
    
    //! データを書込み可能なサンプル数を返す
    UInt32 GetNumPushable() const
    {
        return GetCapacity() - GetNumPoppable();
    }
    
    //! データを書込み可能なサンプル数を返す
    UInt32 GetNumPoppable() const
    {
        UInt32 const wp = write_pos_.load();
        UInt32 const rp = read_pos_.load();
        UInt32 const bs = bufsize_;
        
        return limit(wp + (wp < rp ? bs : 0) - rp);
    }
    
    //! データを追加する
    template<class U>
    Result Push(U const * const *src, UInt32 num_src_channels, UInt32 length)
    {
        auto token = GetPushToken();
        if(!token) { return ErrorCode::kTokenUnavailable; }
        
        UInt32 const wp = write_pos_.load();
        UInt32 const rp = read_pos_.load();
        UInt32 const bs = bufsize_;
        UInt32 const cap = GetCapacity();
        UInt32 const chs = num_channels_;
        
        auto const num_pushable = limit(cap - (wp + (wp < rp ? bs : 0) - rp));
        if(num_pushable < length) { return ErrorCode::kBufferInsufficient; }
        
        for(Int32 ch = 0; ch < chs; ++ch) {
            auto const num_to_copy1 = std::min<UInt32>(bs, wp + length) - wp;
            auto const num_to_copy2 = length - num_to_copy1;
            auto &ch_data = data_[ch];
            
            if(ch < num_src_channels) {
                std::copy_n(src[ch], num_to_copy1, ch_data.begin() + wp);
                std::copy_n(src[ch] + num_to_copy1, num_to_copy2, ch_data.begin());
            } else {
                std::fill_n(ch_data.begin() + wp, num_to_copy1, value_type{});
                std::fill_n(ch_data.begin(), num_to_copy2, value_type{});
            }
        }
        
        write_pos_ = (wp + length) % bs;
        return ErrorCode::kSuccessful;
    }
    
    //! データを取り出し、destに上書きする
    template<class U>
    Result PopOverwrite(U **dest, Int32 num_dest_channels, UInt32 num_required)
    {
        return PopImpl(dest, num_dest_channels, num_required, [](auto src, auto len, auto dest) {
            std::copy(src, src + len, dest);
        });
    }
    
    //! データを取り出し、destに加算する。
    template<class U>
    Result PopAdd(U **dest, UInt32 num_dest_channels, UInt32 num_required)
    {
        return PopImpl(dest, num_dest_channels, num_required, [](auto src, auto len, auto dest) {
            std::transform(src, src + len, dest, dest, std::plus{});
        });
    }

    //! @tparam F is void(*function)(T *src, UInt32 length, U *dest);
    template<class U, class F>
    Result PopImpl(U **dest, UInt32 num_dest_channels, UInt32 num_required, F f)
    {
        auto token = GetPopToken();
        if(!token) { return ErrorCode::kTokenUnavailable; }
        
        UInt32 const wp = write_pos_.load();
        UInt32 const rp = read_pos_.load();
        UInt32 const bs = bufsize_;
        UInt32 const chs = num_channels_;
        
        auto const num_poppable = limit(wp + (wp < rp ? bs : 0) - rp);
        if(num_poppable < num_required) { return ErrorCode::kBufferInsufficient; }
        
        for(Int32 ch = 0; ch < chs; ++ch) {
            auto const num_to_copy1 = std::min<UInt32>(bs, rp + num_required) - rp;
            auto const num_to_copy2 = num_required - num_to_copy1;
            auto const &ch_data = data_[ch];
            
            if(ch < num_dest_channels) {
                f(ch_data.begin() + rp, num_to_copy1, dest[ch]);
                f(ch_data.begin(), num_to_copy2, dest[ch] + num_to_copy1);
            }
        }
        
        read_pos_ = (rp + num_required) % bs;
        return ErrorCode::kSuccessful;
    }
    
    // 書き込んだ領域をクリアする。
    // @note 実際には、インデックス位置をリセットするだけ。
    // Pop()メンバ関数が別スレッドで実行中の場合は、何もせずにfalseを返す。
    Result Clear()
    {
        if(auto token = GetPopToken()) {
            read_pos_ = write_pos_.load();
            return ErrorCode::kSuccessful;
        }
        
        return ErrorCode::kTokenUnavailable;
    }
    
#if defined(_DEBUG)
    void Dump(std::ostream &os)
    {
        os << "----- DUMP ------\n";
        
        UInt32 wp = write_pos_.load();
        UInt32 const rp = read_pos_.load();
        UInt32 const bs = bufsize_;
        UInt32 const cap = GetCapacity();
        UInt32 const chs = num_channels_;
        
        for(Int32 i = 0; i < bs; ++i) {
            if(i == rp) { os << "------ read pos -----\n"; }
            if(i == wp) { os << "------ write pos -----\n"; }
            
            for(Int32 ch = 0; ch < chs; ++ch) {
                auto data = data_[ch][i % bs];
                os << "[" << ch << "]: " << std::setw(8) << std::setprecision(6) << std::fixed << data;
                if((ch+1) != chs) {
                    os << ", ";
                }
            }
            os << "\n";
        }
        os << std::flush;
    }
#endif
    
private:
    UInt32 num_channels_;
    UInt32 bufsize_;
    std::vector<std::vector<value_type>> data_;
    std::atomic<UInt32> read_pos_;
    std::atomic<UInt32> write_pos_;
    
    SampleCount limit(SampleCount n) const
    {
        return std::min<SampleCount>(GetCapacity(), n);
    }
    
    struct Token
    {
        Token()
        :   is_valid_(false)
        ,   releaser_(nullptr)
        {}
        
        Token(bool is_valid, std::function<void()> releaser)
        :   is_valid_(is_valid)
        ,   releaser_(releaser)
        {}
        
        Token(Token const &) = delete;
        Token & operator=(Token const &) = delete;
        
        Token(Token &&rhs)
        {
            releaser_ = std::move(rhs.releaser_);
            is_valid_ = rhs.is_valid_;
            rhs.is_valid_ = false;
        }
        
        Token & operator=(Token &&rhs)
        {
            releaser_ = std::move(rhs.releaser_);
            is_valid_ = rhs.is_valid_;
            rhs.is_valid_ = false;
            
            return *this;
        }
        
        explicit operator bool() const { return is_valid_; }
        
        ~Token()
        {
            if(is_valid_) {
                releaser_();
            }
        }
        
    private:
        bool is_valid_;
        std::function<void()> releaser_;
    };
    
    std::atomic<bool> push_token_;
    std::atomic<bool> pop_token_;
    
    Token GetPushToken()
    {
        return GetTokenImpl(push_token_);
    };
    
    Token GetPopToken()
    {
        return GetTokenImpl(pop_token_);
    };
    
    Token GetTokenImpl(std::atomic<bool> &target)
    {
        auto const desired = true;
        auto expected = false;
        if(target.compare_exchange_strong(expected, desired)) {
            return Token(true, [&target] {
                auto prev = target.exchange(false);
                assert(prev == true);
            });
        } else {
            return Token();
        }
    }
};

template<class T>
using MultiChannelThreadSafeRingBuffer = ThreadSafeRingBufferImpl<T>;

template<class T>
class SingleChannelThreadSafeRingBuffer
:   private ThreadSafeRingBufferImpl<T>
{
public:
    using base_type = ThreadSafeRingBufferImpl<T>;
    
    SingleChannelThreadSafeRingBuffer(UInt32 capacity)
    :    base_type(1, capacity)
    {}
    
    using typename base_type::Result;
    using typename base_type::ErrorCode;
    using typename base_type::value_type;
    using base_type::GetCapacity;
    using base_type::GetNumPoppable;
    using base_type::GetNumPushable;
    using base_type::Clear;
#if defined(_DEBUG)
    using base_type::Dump;
#endif
    
    template<class U>
    Result Push(U const * src, UInt32 length)
    {
        return base_type::Push(&src, 1, length);
    }
    
    //! データを取り出し、destに上書きする
    template<class U>
    Result PopOverwrite(U *dest, UInt32 num_required)
    {
        return base_type::PopOverwrite(&dest, 1, num_required);
    }
    
    //! データを取り出し、destに加算する。
    template<class U>
    Result PopAdd(U *dest, UInt32 num_required)
    {
        return base_type::PopAdd(&dest, 1, num_required);
    }
};

NS_HWM_END
