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
};

NS_HWM_END
