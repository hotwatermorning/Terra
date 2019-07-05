#include "./LoggingSupport.hpp"

#include <atomic>

NS_HWM_BEGIN

void InitializeDefaultGlobalLogger()
{
    auto logger = std::make_unique<Logger>();
    logger->SetLoggingLevels(std::vector<String>{L"Error", L"Warn", L"Info", L"Debug"});
    ReplaceGlobalLogger(std::move(logger));
}

namespace {
    std::atomic<bool> is_enabled_error_check_assertion_;
}

bool IsEnabledErrorCheckAssertionForLoggingMacros()
{
    return is_enabled_error_check_assertion_.load();
}

void EnableErrorCheckAssertionForLoggingMacros(bool enable)
{
    is_enabled_error_check_assertion_.store(enable);
}

NS_HWM_END
