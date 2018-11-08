#include "LockFactory.hpp"

NS_HWM_BEGIN

LockFactory::LockFactory() {}
LockFactory::~LockFactory() {}

std::unique_lock<std::mutex> LockFactory::make_lock() const {
    return std::unique_lock<std::mutex>(mtx_);
}

NS_HWM_END
