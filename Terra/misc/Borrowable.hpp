#pragma once

#include <memory>
#include <mutex>
#include "./LockFactory.hpp"

NS_HWM_BEGIN

template<class T>
struct Borrowable
{
    using TokenType = UInt64;
    static constexpr TokenType kInvalidToken = 0;
    
    //! from non-realtime thread
    void Set(std::shared_ptr<T> x)
    {
        auto lock = lf_.make_lock();
        auto tmp_released = std::move(released_);
        auto tmp_data = std::move(data_);
        assert(!data_ && !released_);
        data_ = std::move(x);
        token_ += 1;
        lock.unlock();
    }
    
    struct Item final
    {
        T const * get() const { return ptr_.get(); }
        T const * operator->() const { return ptr_.get(); }
        T const & operator*() const
        {
            assert(ptr_);
            return *ptr_;
        }
        
        Item()
        {}
        
        Item(std::shared_ptr<T> &&ptr, UInt64 token, Borrowable<T> *owner)
        :   ptr_(std::move(ptr))
        ,   token_(token)
        ,   owner_(owner)
        {
            assert(owner_);
        }
        
        Item(Item const &rhs) = delete;
        Item & operator=(Item const &rhs) = delete;
        Item(Item &&rhs)
        :   ptr_(std::move(rhs.ptr_))
        ,   token_(rhs.token_)
        ,   owner_(rhs.owner_)
        {
            rhs.owner_ = nullptr;
        }
        
        Item & operator=(Item &&rhs)
        {
            Item(std::move(rhs)).swap(*this);
            return *this;
        }
        
        UInt64 GetToken() const { return token_; }
        
        void swap(Item &rhs)
        {
            std::swap(ptr_, rhs.ptr_);
            std::swap(token_, rhs.token_);
            std::swap(owner_, rhs.owner_);
        }
        
        void reset()
        {
            if(ptr_ == nullptr) { return; }
            
            auto lock = owner_->lf_.make_lock();
            
            if(owner_->token_ == token_) {
                assert(owner_->data_ == nullptr);
                owner_->data_ = std::move(ptr_);
            } else {
                assert(owner_->released_ == nullptr);
                owner_->released_ = std::move(ptr_);
            }
        }
        
        ~Item()
        {
            reset();
        }
        
        bool operator==(Item const &rhs) const
        {
            return  (ptr_ == rhs.ptr_)
            &&      (token_ == rhs.token_)
            &&      (owner_ == rhs.owner_)
            ;
        }
        
        bool operator!=(Item const &rhs) const
        {
            return !(*this == rhs);
        }
        
        explicit operator bool() const { return !!ptr_; }
        
    private:
        std::shared_ptr<T> ptr_;
        Borrowable<T> *owner_ = nullptr;
        UInt64 token_ = kInvalidToken;
    };
    
    Item Borrow()
    {
        auto lock = lf_.make_lock();
        
        auto p = std::move(data_ ? data_ : released_);
        return Item { std::move(p), token_, this };
    }
    
private:
    LockFactory lf_;
    std::shared_ptr<T> data_;
    std::shared_ptr<T> released_;
    UInt64 token_ = kInvalidToken;
};

NS_HWM_END
