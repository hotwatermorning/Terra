#pragma once

#include <mutex>

NS_HWM_BEGIN

class LockFactory final
{
public:
    LockFactory();
    ~LockFactory();
    
private:
    std::mutex mutable mtx_;
    
public:
    [[nodiscard]] std::unique_lock<std::mutex> make_lock() const;
    [[nodiscard]] std::unique_lock<std::mutex> make_lock(std::try_to_lock_t try_to_lock) const;
    [[nodiscard]] std::unique_lock<std::mutex> try_make_lock() const { return make_lock(std::try_to_lock); }
};

NS_HWM_END
