#include "LockFactory.hpp"

NS_HWM_BEGIN

LockFactory::LockFactory() {}
LockFactory::~LockFactory() {}

std::unique_lock<std::mutex> LockFactory::make_lock() const {
    return std::unique_lock<std::mutex>(mtx_);
}

std::unique_lock<std::mutex> LockFactory::make_lock(std::try_to_lock_t try_to_lock) const {
    return std::unique_lock<std::mutex>(mtx_, try_to_lock);
}

NS_HWM_END
